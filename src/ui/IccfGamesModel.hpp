#pragma once

#include <QAbstractTableModel>

#include "infra/iccf/IccfModels.hpp"

namespace sf::client::ui {

class IccfGamesModel final : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column {
        MyTurn = 0,
        Id,
        White,
        Black,
        Event,
        Board,
        TimeLeft,
        Moves,
        ColumnCount
    };

    explicit IccfGamesModel(QObject* parent = nullptr);

    void setGames(QVector<sf::client::infra::iccf::IccfGame> games);
    const sf::client::infra::iccf::IccfGame* gameAt(int row) const;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    QVector<sf::client::infra::iccf::IccfGame> games_;
};

} // namespace sf::client::ui
