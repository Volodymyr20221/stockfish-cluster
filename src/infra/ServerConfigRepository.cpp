#include "infra/ServerConfigRepository.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QDebug>

namespace sf::client::infra {

using sf::client::domain::ServerInfo;

ServerConfigRepository::ServerConfigRepository(std::string path)
    : path_(std::move(path)) {
}

std::vector<ServerInfo> ServerConfigRepository::defaultServers() const {
    ServerInfo s;
    s.id            = "local-1";
    s.name          = "Local SF #1";
    s.host          = "127.0.0.1";
    s.port          = 9000;
    s.cores         = 0;
    s.threadsPerJob = 1;
    s.maxJobs       = 1;
    s.enabled       = true;

    s.tlsEnabled        = false;
    s.tlsServerName     = "";
    s.tlsCaFile         = "";
    s.tlsClientCertFile = "";
    s.tlsClientKeyFile  = "";

    s.runtime.status      = sf::client::domain::ServerStatus::Unknown;
    s.runtime.runningJobs = 0;
    s.runtime.maxJobs     = s.maxJobs;
    s.runtime.loadPercent = 0.0;

    return {s};
}

std::vector<ServerInfo> ServerConfigRepository::load() const {
    QFile file(QString::fromStdString(path_));
    if (!file.exists()) {
        qWarning() << "Servers config not found, using defaults:" << QString::fromStdString(path_);
        return defaultServers();
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Failed to open servers config, using defaults:" << QString::fromStdString(path_);
        return defaultServers();
    }

    QJsonParseError parseErr{};
    const auto doc = QJsonDocument::fromJson(file.readAll(), &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "Invalid server config, using defaults:" << parseErr.errorString();
        return defaultServers();
    }

    const auto rootObj = doc.object();
    if (!rootObj.contains(QStringLiteral("servers")) || !rootObj.value(QStringLiteral("servers")).isArray()) {
        qWarning() << "Invalid server config, using defaults:" << "missing 'servers' array";
        return defaultServers();
    }

    const auto arr = rootObj.value(QStringLiteral("servers")).toArray();
    std::vector<ServerInfo> servers;
    servers.reserve(arr.size());

    for (const auto& v : arr) {
        if (!v.isObject()) {
            continue;
        }
        const auto o = v.toObject();

        ServerInfo s;
        s.id   = o.value(QStringLiteral("id")).toString().toStdString();
        s.name = o.value(QStringLiteral("name")).toString().toStdString();
        s.host = o.value(QStringLiteral("host")).toString().toStdString();
        s.port = o.value(QStringLiteral("port")).toInt(0);

        // Minimal validation
        if (s.id.empty() || s.host.empty() || s.port <= 0) {
            qWarning() << "Invalid server entry in config (missing id/host/port), skipping.";
            continue;
        }
        if (s.name.empty()) {
            s.name = s.id;
        }

        s.cores         = o.value(QStringLiteral("cores")).toInt(0);
        s.threadsPerJob = o.value(QStringLiteral("threads_per_job")).toInt(1);
        s.maxJobs       = o.value(QStringLiteral("max_jobs")).toInt(1);
        s.enabled       = o.value(QStringLiteral("enabled")).toBool(true);

        s.tlsEnabled        = o.value(QStringLiteral("tls_enabled")).toBool(false);
        s.tlsServerName     = o.value(QStringLiteral("tls_server_name")).toString().toStdString();
        s.tlsCaFile         = o.value(QStringLiteral("tls_ca_file")).toString().toStdString();
        s.tlsClientCertFile = o.value(QStringLiteral("tls_client_cert_file")).toString().toStdString();
        s.tlsClientKeyFile  = o.value(QStringLiteral("tls_client_key_file")).toString().toStdString();

        s.runtime.status      = sf::client::domain::ServerStatus::Unknown;
        s.runtime.runningJobs = 0;
        s.runtime.maxJobs     = s.maxJobs;
        s.runtime.loadPercent = 0.0;

        servers.push_back(std::move(s));
    }

    if (servers.empty()) {
        qWarning() << "Invalid server config, using defaults:" << "no valid servers";
        return defaultServers();
    }

    return servers;
}

void ServerConfigRepository::save(const std::vector<ServerInfo>& servers) const {
    QJsonArray arr;
    for (const auto& s : servers) {
        QJsonObject o;
        o.insert(QStringLiteral("id"),             QString::fromStdString(s.id));
        o.insert(QStringLiteral("name"),           QString::fromStdString(s.name));
        o.insert(QStringLiteral("host"),           QString::fromStdString(s.host));
        o.insert(QStringLiteral("port"),           s.port);
        o.insert(QStringLiteral("cores"),          s.cores);
        o.insert(QStringLiteral("threads_per_job"), s.threadsPerJob);
        o.insert(QStringLiteral("max_jobs"),       s.maxJobs);
        o.insert(QStringLiteral("enabled"),        s.enabled);

        o.insert(QStringLiteral("tls_enabled"),           s.tlsEnabled);
        o.insert(QStringLiteral("tls_server_name"),       QString::fromStdString(s.tlsServerName));
        o.insert(QStringLiteral("tls_ca_file"),           QString::fromStdString(s.tlsCaFile));
        o.insert(QStringLiteral("tls_client_cert_file"),  QString::fromStdString(s.tlsClientCertFile));
        o.insert(QStringLiteral("tls_client_key_file"),   QString::fromStdString(s.tlsClientKeyFile));
        arr.push_back(o);
    }

    QJsonObject root;
    root.insert(QStringLiteral("servers"), arr);

    QFile file(QString::fromStdString(path_));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Failed to write servers config:" << QString::fromStdString(path_);
        return;
    }

    QJsonDocument doc(root);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
}

} // namespace sf::client::infra
