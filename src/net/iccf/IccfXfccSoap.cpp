#include "net/iccf/IccfXfccSoap.hpp"

#include <QNetworkRequest>

namespace sf::client::net::iccf {

IccfXfccSoap::IccfXfccSoap(QObject* parent)
    : QObject(parent) {
}

void IccfXfccSoap::setEndpointUrl(const QUrl& url) {
    endpoint_ = url;
}

QUrl IccfXfccSoap::endpointUrl() const {
    return endpoint_;
}

QString IccfXfccSoap::soapAction(Operation op) {
    // SOAPAction values are defined by the XfccBasic service and shown in ICCF's sample requests.
    // We keep them exactly as specified (case-sensitive).
    switch (op) {
        case Operation::GetMyGames:
            return QStringLiteral("http://www.bennedik.com/webservices/XfccBasic/GetMyGames");
        case Operation::MakeAMove:
            return QStringLiteral("http://www.bennedik.com/webservices/XfccBasic/MakeAMove");
        case Operation::MakeAMove2:
            return QStringLiteral("http://www.bennedik.com/webservices/XfccBasic/MakeAMove2");
    }
    return QString();
}

QString IccfXfccSoap::xmlEscape(const QString& s) {
    QString out;
    out.reserve(s.size() + 16);
    for (QChar ch : s) {
        switch (ch.unicode()) {
            case '&': out += QStringLiteral("&amp;"); break;
            case '<': out += QStringLiteral("&lt;"); break;
            case '>': out += QStringLiteral("&gt;"); break;
            case '"': out += QStringLiteral("&quot;"); break;
            case '\'': out += QStringLiteral("&apos;"); break;
            default: out += ch; break;
        }
    }
    return out;
}

QByteArray IccfXfccSoap::buildGetMyGamesEnvelope(const QString& username, const QString& password) {
    // SOAP 1.1 envelope.
    // Namespace is per ICCF service samples.
    const QString u = xmlEscape(username);
    const QString p = xmlEscape(password);

    QString xml;
    xml.reserve(512 + u.size() + p.size());
    xml += QStringLiteral("<?xml version=\"1.0\" encoding=\"utf-8\"?>");
    xml += QStringLiteral("<soap:Envelope xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
                          "xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\" "
                          "xmlns:soap=\"http://schemas.xmlsoap.org/soap/envelope/\">"
                          "<soap:Body>"
                          "<GetMyGames xmlns=\"http://www.bennedik.com/webservices/XfccBasic\">"
                          "<username>");
    xml += u;
    xml += QStringLiteral("</username><password>");
    xml += p;
    xml += QStringLiteral("</password></GetMyGames></soap:Body></soap:Envelope>");

    return xml.toUtf8();
}

QNetworkRequest IccfXfccSoap::makeRequest(Operation op) const {
    QNetworkRequest req(endpoint_);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("text/xml; charset=utf-8"));
    req.setRawHeader("SOAPAction", QByteArray("\"") + soapAction(op).toUtf8() + QByteArray("\""));
    // Some servers are picky; disable caching.
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    return req;
}

void IccfXfccSoap::post(Operation op, const QByteArray& soapEnvelope) {
    QNetworkRequest req = makeRequest(op);
    QNetworkReply* reply = nam_.post(req, soapEnvelope);

    QObject::connect(reply, &QNetworkReply::finished, this, [this, op, reply]() {
        const QByteArray payload = reply->readAll();
        const bool ok = (reply->error() == QNetworkReply::NoError);
        const QString err = ok ? QString() : reply->errorString();
        reply->deleteLater();
        emit finished(op, ok, payload, err);
    });
}

} // namespace sf::client::net::iccf
