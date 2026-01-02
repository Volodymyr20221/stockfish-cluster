#include "net/JobConnection.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSslConfiguration>
#include <QDebug>

namespace sf::client::net {

namespace {

bool readFileBytes(const QString& path, QByteArray& out) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return false;
    }
    out = f.readAll();
    return true;
}

} // namespace

JobConnection::JobConnection(const QString& serverId,
                             const QString& host,
                             quint16 port,
                             bool tlsEnabled,
                             const QString& tlsServerName,
                             const QString& tlsCaFile,
                             const QString& tlsClientCertFile,
                             const QString& tlsClientKeyFile,
                             QObject* parent)
    : QObject(parent)
    , serverId_(serverId)
    , host_(host)
    , port_(port)
    , tlsEnabled_(tlsEnabled)
    , tlsServerName_(tlsServerName)
    , tlsCaFile_(tlsCaFile)
    , tlsClientCertFile_(tlsClientCertFile)
    , tlsClientKeyFile_(tlsClientKeyFile) {

    socket_.setSocketOption(QAbstractSocket::LowDelayOption, 1);

    connect(&socket_, &QSslSocket::readyRead, this, &JobConnection::onReadyRead);
    connect(&socket_, &QSslSocket::connected, this, &JobConnection::onConnected);
    connect(&socket_, &QSslSocket::disconnected, this, &JobConnection::onDisconnected);
    connect(&socket_, &QSslSocket::encrypted, this, &JobConnection::onEncrypted);
    connect(&socket_, &QSslSocket::sslErrors, this, &JobConnection::onSslErrors);
    connect(&socket_, &QSslSocket::errorOccurred, this, &JobConnection::onSocketError);
}

void JobConnection::connectToHost() {
    // Avoid spamming connectToHost() while Qt is in HostLookup/Connecting states.
    if (socket_.state() != QAbstractSocket::UnconnectedState) {
        return;
    }

    if (tlsEnabled_) {
        if (!configureTls()) {
            qWarning() << "TLS configuration failed for" << serverId_ << ", refusing to connect.";
            return;
        }
        socket_.connectToHostEncrypted(host_, port_);
    } else {
        socket_.connectToHost(host_, port_);
    }
}

void JobConnection::onConnected() {
    // For TLS we only become message-ready after the handshake (onEncrypted).
    if (!tlsEnabled_) {
        emit connectionReady(serverId_);
    }
}

void JobConnection::sendJson(const QJsonObject& obj) {
    if (!isConnected()) {
        return;
    }
    const auto payload = QJsonDocument(obj).toJson(QJsonDocument::Compact) + QByteArrayLiteral("\n");
    socket_.write(payload);
}

void JobConnection::onReadyRead() {
    buffer_.append(socket_.readAll());
    processIncomingData();
}

void JobConnection::processIncomingData() {
    while (true) {
        const int newlineIndex = buffer_.indexOf('\n');
        if (newlineIndex < 0) {
            break;
        }

        const QByteArray line = buffer_.left(newlineIndex);
        buffer_.remove(0, newlineIndex + 1);

        if (line.trimmed().isEmpty()) {
            continue;
        }
        handleJsonLine(line);
    }
}

void JobConnection::handleJsonLine(const QByteArray& line) {
    QJsonParseError err{};
    const auto      doc = QJsonDocument::fromJson(line, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "Failed to parse JSON from server" << serverId_ << ":" << err.errorString()
                   << "line:" << QString::fromUtf8(line.left(200));
        return;
    }
    emit jsonReceived(serverId_, doc.object());
}

void JobConnection::onDisconnected() {
    emit disconnected(serverId_);
}

void JobConnection::onEncrypted() {
    qDebug() << "TLS handshake completed for" << serverId_ << "peer:" << socket_.peerName();
    emit connectionReady(serverId_);
}

void JobConnection::onSslErrors(const QList<QSslError>& errors) {
    logSslErrorsAndAbort(errors);
}

void JobConnection::onSocketError(QAbstractSocket::SocketError error) {
    Q_UNUSED(error);
    qWarning() << "Socket error for" << serverId_ << ":" << socket_.errorString();
}

QString JobConnection::resolvePath(const QString& path) const {
    if (path.trimmed().isEmpty()) {
        return path;
    }
    QFileInfo fi(path);
    if (fi.isAbsolute()) {
        return fi.absoluteFilePath();
    }
    const auto base = QCoreApplication::applicationDirPath();
    return QFileInfo(base + QLatin1Char('/') + path).absoluteFilePath();
}

bool JobConnection::validateTlsFilePaths(const QString& caPath,
                                        const QString& certPath,
                                        const QString& keyPath) const {
    if (caPath.trimmed().isEmpty() || certPath.trimmed().isEmpty() || keyPath.trimmed().isEmpty()) {
        qWarning() << "TLS enabled but TLS paths are empty for" << serverId_;
        return false;
    }

    if (!QFileInfo::exists(caPath)) {
        qWarning() << "TLS CA file not found:" << caPath;
        return false;
    }
    if (!QFileInfo::exists(certPath)) {
        qWarning() << "TLS client certificate not found:" << certPath;
        return false;
    }
    if (!QFileInfo::exists(keyPath)) {
        qWarning() << "TLS client key not found:" << keyPath;
        return false;
    }
    return true;
}

bool JobConnection::loadCaCertificates(const QString& caPath, QList<QSslCertificate>& out) const {
    QByteArray bytes;
    if (!readFileBytes(caPath, bytes)) {
        qWarning() << "Failed to open CA file" << caPath;
        return false;
    }
    out = QSslCertificate::fromData(bytes, QSsl::Pem);
    if (out.isEmpty()) {
        qWarning() << "Failed to parse CA certificates from" << caPath;
        return false;
    }
    return true;
}

bool JobConnection::loadClientCertificate(const QString& certPath, QSslCertificate& out) const {
    QByteArray bytes;
    if (!readFileBytes(certPath, bytes)) {
        qWarning() << "Failed to open client certificate" << certPath;
        return false;
    }
    const auto certs = QSslCertificate::fromData(bytes, QSsl::Pem);
    if (certs.isEmpty()) {
        qWarning() << "Failed to parse client certificate from" << certPath;
        return false;
    }
    out = certs.first();
    return true;
}

bool JobConnection::loadClientKey(const QString& keyPath, QSslKey& out) const {
    QByteArray bytes;
    if (!readFileBytes(keyPath, bytes)) {
        qWarning() << "Failed to open client key" << keyPath;
        return false;
    }

    // Try RSA then EC.
    out = QSslKey(bytes, QSsl::Rsa, QSsl::Pem);
    if (out.isNull()) {
        out = QSslKey(bytes, QSsl::Ec, QSsl::Pem);
    }
    if (out.isNull()) {
        qWarning() << "Failed to parse client key from" << keyPath;
        return false;
    }
    return true;
}

void JobConnection::applyTlsConfiguration(const QList<QSslCertificate>& caCerts,
                                         const QSslCertificate& clientCert,
                                         const QSslKey& clientKey) {
    QSslConfiguration cfg = socket_.sslConfiguration();
    cfg.setProtocol(QSsl::SecureProtocols);
    cfg.setCaCertificates(caCerts);
    socket_.setSslConfiguration(cfg);

    socket_.setLocalCertificate(clientCert);
    socket_.setPrivateKey(clientKey);
    socket_.setPeerVerifyMode(QSslSocket::VerifyPeer);

    const QString verifyName =
        !tlsServerName_.trimmed().isEmpty() ? tlsServerName_.trimmed() : host_;
    socket_.setPeerVerifyName(verifyName);
}

bool JobConnection::configureTls() {
    if (!QSslSocket::supportsSsl()) {
        qWarning() << "Qt SSL support is unavailable (QSslSocket::supportsSsl()=false).";
        return false;
    }

    const QString caPath   = resolvePath(tlsCaFile_);
    const QString certPath = resolvePath(tlsClientCertFile_);
    const QString keyPath  = resolvePath(tlsClientKeyFile_);

    if (!validateTlsFilePaths(caPath, certPath, keyPath)) {
        return false;
    }

    QList<QSslCertificate> caCerts;
    if (!loadCaCertificates(caPath, caCerts)) {
        return false;
    }

    QSslCertificate clientCert;
    if (!loadClientCertificate(certPath, clientCert)) {
        return false;
    }

    QSslKey clientKey;
    if (!loadClientKey(keyPath, clientKey)) {
        return false;
    }

    applyTlsConfiguration(caCerts, clientCert, clientKey);
    return true;
}

void JobConnection::logSslErrorsAndAbort(const QList<QSslError>& errors) {
    for (const auto& err : errors) {
        qWarning() << "TLS error for" << serverId_ << ":" << err.errorString();
    }
    socket_.abort();
}

} // namespace sf::client::net
