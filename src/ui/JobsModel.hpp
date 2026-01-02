#pragma once

#include <QAbstractTableModel>
#include <optional>
#include <vector>

#include "domain/domain_model.hpp"

namespace sf::client::ui {

class JobsModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit JobsModel(QObject* parent = nullptr);

    void setJobs(const std::vector<sf::client::domain::Job>& jobs);
    void upsertJob(const sf::client::domain::Job& job);
    void removeJob(const sf::client::domain::JobId& id);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    std::optional<sf::client::domain::Job> jobAtRow(int row) const;

private:
    enum Column {
        ColId = 0,
        ColOpponent,
        ColServer,
        ColStatus,
        ColDepth,
        ColEval,
        ColLastUpdate,
        ColumnCount
    };

    QVariant displayData(const sf::client::domain::Job& job, Column col) const;
    QVariant alignmentData(Column col) const;

    std::vector<sf::client::domain::Job> jobs_;
};

} // namespace sf::client::ui
