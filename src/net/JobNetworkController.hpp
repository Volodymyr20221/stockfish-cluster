#pragma once

#include <QObject>
#include <QTimer>
#include <memory>
#include <unordered_map>

#include <QJsonObject>

#include "domain/domain_model.hpp"
#include "app/JobManager.hpp"
#include "app/ServerManager.hpp"
#include "net/JobConnection.hpp"

namespace sf::client::net {

// Orchestrates communication between JobManager and remote workers.
class JobNetworkController : public QObject {
    Q_OBJECT
public:
    JobNetworkController(sf::client::app::JobManager& jobManager,
                         sf::client::app::ServerManager& serverManager,
                         QObject* parent = nullptr);

    // Create JobConnection objects for the given servers and connect.
    void initializeConnections(
        const std::vector<sf::client::domain::ServerInfo>& servers);

    // Called from JobManager callbacks.
    void handleJobAddedOrUpdated(const sf::client::domain::Job& job);
    void handleJobRemoved(const sf::client::domain::Job& job);

private slots:
    void onConnectionReady(const QString& serverId);
    void onJsonReceived(const QString& serverId, const QJsonObject& obj);
    void onConnectionDisconnected(const QString& serverId);
    void onPingTimeout();

private:
    void sendPing();
    void sendJobsListRequest(const QString& serverId);

    // Protocol helpers (keep message parsing on one abstraction level)
    QString detectMessageType(const QJsonObject& obj) const;
    void handleJobUpdateMessage(const QString& serverId, const QJsonObject& obj);
    void handleServerStatusMessage(const QString& serverId, const QJsonObject& obj);
    void handleJobsListMessage(const QString& serverId, const QJsonObject& obj);


    sf::client::app::JobManager&    jobManager_;
    sf::client::app::ServerManager& serverManager_;

    std::unordered_map<std::string, std::unique_ptr<JobConnection>> connections_;
    QTimer                                                           pingTimer_;
};

} // namespace sf::client::net
