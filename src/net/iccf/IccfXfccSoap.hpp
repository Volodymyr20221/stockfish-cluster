#pragma once

#include <QObject>

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrl>

namespace sf::client::net::iccf {

// Minimal SOAP 1.1 transport for ICCF XfccBasic web service.
//
// ICCF publishes XfccBasic at https://www.iccf.com/XfccBasic.asmx with operations
// GetMyGames / MakeAMove / MakeAMove2.
//
// This class only deals with HTTPS POST + SOAPAction + XML envelope.
// Parsing of method-specific results should live in a higher layer.
class IccfXfccSoap final : public QObject {
    Q_OBJECT

public:
    enum class Operation {
        GetMyGames,
        MakeAMove,
        MakeAMove2,
    };
    Q_ENUM(Operation)

    explicit IccfXfccSoap(QObject* parent = nullptr);

    void setEndpointUrl(const QUrl& url);
    QUrl endpointUrl() const;

    // SOAPAction strings for SOAP 1.1. These follow ICCF's XfccBasic service samples.
    static QString soapAction(Operation op);

    // Builds SOAP 1.1 envelope for GetMyGames.
    static QByteArray buildGetMyGamesEnvelope(const QString& username, const QString& password);

    // Sends a SOAP 1.1 request.
    // Emits finished(op, ok, payload, errorMessage).
    void post(Operation op, const QByteArray& soapEnvelope);

signals:
    void finished(sf::client::net::iccf::IccfXfccSoap::Operation op,
                  bool ok,
                  QByteArray payload,
                  QString errorMessage);

private:
    static QString xmlEscape(const QString& s);

    QNetworkRequest makeRequest(Operation op) const;

    QNetworkAccessManager nam_;
    QUrl endpoint_{QStringLiteral("https://www.iccf.com/XfccBasic.asmx")};
};

} // namespace sf::client::net::iccf
