#include "ui/IccfGamesModel.hpp"

#include <QVariant>

namespace sf::client::ui {

IccfGamesModel::IccfGamesModel(QObject* parent)
    : QAbstractTableModel(parent) {
}

void IccfGamesModel::setGames(QVector<sf::client::infra::iccf::IccfGame> games) {
    beginResetModel();
    games_ = std::move(games);
    endResetModel();
}

const sf::client::infra::iccf::IccfGame* IccfGamesModel::gameAt(int row) const {
    if (row < 0 || row >= games_.size()) return nullptr;
    return &games_[row];
}

int IccfGamesModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return games_.size();
}

int IccfGamesModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return ColumnCount;
}

QVariant IccfGamesModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) return {};
    const int row = index.row();
    const int col = index.column();
    if (row < 0 || row >= games_.size()) return {};
    const auto& g = games_[row];

    if (role == Qt::DisplayRole) {
        switch (col) {
            case MyTurn:  return g.myTurn ? QStringLiteral("✓") : QString();
            case Id:      return g.id;
            case White:   return g.white;
            case Black:   return g.black;
            case Event:   return g.event;
            case Board:   return g.board;
            case TimeLeft: {
                const int d = g.daysPlayer;
                const int h = g.hoursPlayer;
                const int m = g.minutesPlayer;
                return QStringLiteral("%1d %2:%3")
                    .arg(d)
                    .arg(h, 2, 10, QChar('0'))
                    .arg(m, 2, 10, QChar('0'));
            }
            case Moves:   return g.moves;
            default:      return {};
        }
    }

    if (role == Qt::TextAlignmentRole) {
        if (col == MyTurn || col == Id) return Qt::AlignCenter;
    }

    return {};
}

QVariant IccfGamesModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal) return {};
    if (role != Qt::DisplayRole) return {};

    switch (section) {
        case MyTurn: return QStringLiteral("My");
        case Id: return QStringLiteral("ID");
        case White: return QStringLiteral("White");
        case Black: return QStringLiteral("Black");
        case Event: return QStringLiteral("Event");
        case Board: return QStringLiteral("Board");
        case TimeLeft: return QStringLiteral("Time left");
        case Moves: return QStringLiteral("Moves");
        default: return {};
    }
}

} // namespace sf::client::ui
