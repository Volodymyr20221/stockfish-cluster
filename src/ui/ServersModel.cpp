#include "ui/ServersModel.hpp"

#include <QBrush>
#include <QColor>
#include <QString>

namespace sf::client::ui {

using sf::client::domain::ServerInfo;
using sf::client::domain::ServerStatus;

namespace {

QString statusText(ServerStatus s) {
    switch (s) {
        case ServerStatus::Online:
            return QStringLiteral("Online");
        case ServerStatus::Degraded:
            return QStringLiteral("Busy");
        case ServerStatus::Offline:
            return QStringLiteral("Offline");
        case ServerStatus::Unknown:
        default:
            return QStringLiteral("Unknown");
    }
}

QBrush statusBrush(ServerStatus s) {
    switch (s) {
        case ServerStatus::Online:
            return QBrush(QColor(200, 255, 200)); // light green
        case ServerStatus::Degraded:
            return QBrush(QColor(255, 255, 200)); // light yellow
        case ServerStatus::Offline:
            return QBrush(QColor(255, 200, 200)); // light red
        case ServerStatus::Unknown:
        default:
            return QBrush(QColor(230, 230, 230)); // light gray
    }
}

} // namespace

ServersModel::ServersModel(QObject* parent)
    : QAbstractTableModel(parent) {
}

void ServersModel::setServers(const std::vector<ServerInfo>& servers) {
    beginResetModel();
    servers_ = servers;
    endResetModel();
}

int ServersModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(servers_.size());
}

int ServersModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant ServersModel::displayData(const ServerInfo& s, Column col) const {
    switch (col) {
        case ColId:
            return QString::fromStdString(s.id);
        case ColName:
            return QString::fromStdString(s.name);
        case ColHost:
            return QString::fromStdString(s.host);
        case ColPort:
            return s.port;
        case ColStatus:
            return statusText(s.runtime.status);
        case ColLoad:
            return QStringLiteral("%1 %").arg(static_cast<int>(s.runtime.loadPercent));
        case ColRunningJobs:
            return s.runtime.runningJobs;
        case ColCores:
            return s.cores;
        case ColThreadsPerJob:
            return s.threadsPerJob;
        case ColMaxJobs:
            return (s.runtime.maxJobs > 0 ? s.runtime.maxJobs : s.maxJobs);
        default:
            return {};
    }
}

QVariant ServersModel::backgroundData(const ServerInfo& s, Column col) const {
    if (col != ColStatus) {
        return {};
    }
    return statusBrush(s.runtime.status);
}

QVariant ServersModel::alignmentData(Column col) const {
    switch (col) {
        case ColPort:
        case ColLoad:
        case ColRunningJobs:
        case ColCores:
        case ColThreadsPerJob:
        case ColMaxJobs:
            return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
        case ColStatus:
            return static_cast<int>(Qt::AlignCenter);
        default:
            return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
    }
}

QVariant ServersModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) {
        return {};
    }

    const int row = index.row();
    const int col = index.column();

    if (row < 0 || row >= static_cast<int>(servers_.size())) {
        return {};
    }

    const ServerInfo& s = servers_[row];
    const Column colEnum = static_cast<Column>(col);

    if (role == Qt::DisplayRole) {
        return displayData(s, colEnum);
    }
    if (role == Qt::BackgroundRole) {
        return backgroundData(s, colEnum);
    }
    if (role == Qt::TextAlignmentRole) {
        return alignmentData(colEnum);
    }

    return {};
}

QVariant ServersModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        switch (section) {
            case ColId:
                return QStringLiteral("Id");
            case ColName:
                return QStringLiteral("Name");
            case ColHost:
                return QStringLiteral("Host");
            case ColPort:
                return QStringLiteral("Port");
            case ColStatus:
                return QStringLiteral("Status");
            case ColLoad:
                return QStringLiteral("Load");
            case ColRunningJobs:
                return QStringLiteral("Jobs");
            case ColCores:
                return QStringLiteral("Cores");
            case ColThreadsPerJob:
                return QStringLiteral("Threads/job");
            case ColMaxJobs:
                return QStringLiteral("Max jobs");
            default:
                break;
        }
    }
    return QAbstractTableModel::headerData(section, orientation, role);
}

} // namespace sf::client::ui
