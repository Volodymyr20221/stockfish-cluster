#pragma once

#include <QObject>

#include <QTimer>

#include "infra/iccf/IccfModels.hpp"
#include "net/iccf/IccfXfccSoap.hpp"

namespace sf::client::app {

// Orchestrates periodic synchronization with ICCF using the XfccBasic SOAP service.
// MVP scope: GetMyGames only (read-only).
class IccfSyncManager final : public QObject {
    Q_OBJECT

public:
    struct Credentials {
        QString endpointUrl;
        QString username;
        QString password;
    };

    explicit IccfSyncManager(QObject* parent = nullptr);

    void setCredentials(Credentials c);
    Credentials credentials() const;

    // One-shot refresh. Safe to call even if polling is enabled.
    void refreshNow();

    // Optional polling.
    void startPolling(int intervalMs);
    void stopPolling();
    bool isPolling() const;

signals:
    void status(const QString& text);
    void error(const QString& message);
    void gamesUpdated(QVector<sf::client::infra::iccf::IccfGame> games);

private:
    void handleSoapFinished(sf::client::net::iccf::IccfXfccSoap::Operation op,
                            bool ok,
                            QByteArray payload,
                            QString errorMessage);

    Credentials creds_;
    sf::client::net::iccf::IccfXfccSoap soap_;
    QTimer pollTimer_;

    bool busy_{false};
    QVector<sf::client::infra::iccf::IccfGame> games_;
};

} // namespace sf::client::app
