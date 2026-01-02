#pragma once

#include <QAbstractTableModel>
#include <vector>

#include "domain/domain_model.hpp"

namespace sf::client::ui {

class ServersModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit ServersModel(QObject* parent = nullptr);

    void setServers(const std::vector<sf::client::domain::ServerInfo>& servers);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

private:
    enum Column {
        ColId = 0,
        ColName,
        ColHost,
        ColPort,
        ColStatus,
        ColLoad,
        ColRunningJobs,
        ColCores,
        ColThreadsPerJob,
        ColMaxJobs,
        ColumnCount
    };

    QVariant displayData(const sf::client::domain::ServerInfo& server, Column col) const;
    QVariant backgroundData(const sf::client::domain::ServerInfo& server, Column col) const;
    QVariant alignmentData(Column col) const;

    std::vector<sf::client::domain::ServerInfo> servers_;
};

} // namespace sf::client::ui
