#pragma once

#include <QObject>
#include <QJsonObject>
#include <QSslSocket>
#include <QSslError>
#include <QSslCertificate>
#include <QSslKey>

namespace sf::client::net {

// Thin wrapper over QTcpSocket for line-delimited JSON messages.
class JobConnection : public QObject {
    Q_OBJECT
public:
    JobConnection(const QString& serverId,
                  const QString& host,
                  quint16 port,
                  bool tlsEnabled,
                  const QString& tlsServerName,
                  const QString& tlsCaFile,
                  const QString& tlsClientCertFile,
                  const QString& tlsClientKeyFile,
                  QObject* parent = nullptr);

    const QString& serverId() const noexcept { return serverId_; }

    // Ready-to-send state.
    bool isConnected() const noexcept {
        if (tlsEnabled_) {
            return socket_.state() == QAbstractSocket::ConnectedState && socket_.isEncrypted();
        }
        return socket_.state() == QAbstractSocket::ConnectedState;
    }

    bool isConnectingOrConnected() const noexcept {
        const auto st = socket_.state();
        return st == QAbstractSocket::ConnectedState ||
               st == QAbstractSocket::ConnectingState ||
               st == QAbstractSocket::HostLookupState;
    }

    void connectToHost();
    void sendJson(const QJsonObject& obj);

signals:
    // Socket is ready to exchange JSON messages.
    // For TLS connections this is emitted after the TLS handshake.
    void connectionReady(const QString& serverId);
    void jsonReceived(const QString& serverId, const QJsonObject& obj);
    void disconnected(const QString& serverId);

private slots:
    void onReadyRead();
    void onConnected();
    void onDisconnected();
    void onEncrypted();
    void onSslErrors(const QList<QSslError>& errors);
    void onSocketError(QAbstractSocket::SocketError error);

private:
    void processIncomingData();
    void handleJsonLine(const QByteArray& line);

    bool configureTls();
    bool validateTlsFilePaths(const QString& caPath,
                              const QString& certPath,
                              const QString& keyPath) const;
    bool loadCaCertificates(const QString& caPath, QList<QSslCertificate>& out) const;
    bool loadClientCertificate(const QString& certPath, QSslCertificate& out) const;
    bool loadClientKey(const QString& keyPath, QSslKey& out) const;
    void applyTlsConfiguration(const QList<QSslCertificate>& caCerts,
                               const QSslCertificate& clientCert,
                               const QSslKey& clientKey);
    void logSslErrorsAndAbort(const QList<QSslError>& errors);

    QString resolvePath(const QString& path) const;

    QString    serverId_;
    QString    host_;
    quint16    port_{0};
    bool       tlsEnabled_{false};
    QString    tlsServerName_;
    QString    tlsCaFile_;
    QString    tlsClientCertFile_;
    QString    tlsClientKeyFile_;

    QSslSocket socket_;
    QByteArray buffer_;
};

} // namespace sf::client::net