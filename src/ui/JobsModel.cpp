#include "ui/JobsModel.hpp"
#include "ui/UiFormatters.hpp"

#include <QString>

namespace sf::client::ui {

using sf::client::domain::Job;
using sf::client::domain::JobId;
using sf::client::domain::ScoreType;

JobsModel::JobsModel(QObject* parent)
    : QAbstractTableModel(parent) {
}

void JobsModel::setJobs(const std::vector<Job>& jobs) {
    beginResetModel();
    jobs_ = jobs;
    endResetModel();
}

void JobsModel::upsertJob(const Job& job) {
    for (int row = 0; row < static_cast<int>(jobs_.size()); ++row) {
        if (jobs_[row].id == job.id) {
            jobs_[row] = job;
            const QModelIndex topLeft     = index(row, 0);
            const QModelIndex bottomRight = index(row, ColumnCount - 1);
            emit dataChanged(topLeft, bottomRight);
            return;
        }
    }

    const int newRow = static_cast<int>(jobs_.size());
    beginInsertRows(QModelIndex(), newRow, newRow);
    jobs_.push_back(job);
    endInsertRows();
}

void JobsModel::removeJob(const JobId& id) {
    for (int row = 0; row < static_cast<int>(jobs_.size()); ++row) {
        if (jobs_[row].id == id) {
            beginRemoveRows(QModelIndex(), row, row);
            jobs_.erase(jobs_.begin() + row);
            endRemoveRows();
            return;
        }
    }
}

int JobsModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(jobs_.size());
}

int JobsModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant JobsModel::displayData(const Job& job, Column col) const {
    switch (col) {
        case ColId:
            return QString::fromStdString(job.id);

        case ColOpponent:
            return QString::fromStdString(job.opponent);

        case ColServer:
            return job.assignedServer ? QString::fromStdString(*job.assignedServer) : QStringLiteral("-");

        case ColStatus:
            return QString::fromStdString(sf::client::domain::to_string(job.status));

        case ColDepth:
            return job.snapshot.depth ? QVariant(*job.snapshot.depth) : QVariant();

        case ColEval:
            if (job.snapshot.score.type != ScoreType::None) {
                return QString::fromStdString(sf::client::domain::to_string(job.snapshot.score));
            }
            return {};

        case ColLastUpdate:
            return sf::client::ui::fmt::formatLocalIso(job.lastUpdateAt);

        default:
            return {};
    }
}

QVariant JobsModel::alignmentData(Column col) const {
    // Make numeric columns easier to read.
    switch (col) {
        case ColDepth:
        case ColEval:
            return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
        default:
            return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
    }
}

QVariant JobsModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) {
        return {};
    }

    const int row = index.row();
    const int col = index.column();

    if (row < 0 || row >= static_cast<int>(jobs_.size())) {
        return {};
    }

    const Job& job = jobs_[row];

    if (role == Qt::DisplayRole) {
        return displayData(job, static_cast<Column>(col));
    }

    if (role == Qt::TextAlignmentRole) {
        return alignmentData(static_cast<Column>(col));
    }

    return {};
}

QVariant JobsModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        switch (section) {
            case ColId:
                return QStringLiteral("Job ID");
            case ColOpponent:
                return QStringLiteral("Opponent");
            case ColServer:
                return QStringLiteral("Server");
            case ColStatus:
                return QStringLiteral("Status");
            case ColDepth:
                return QStringLiteral("Depth");
            case ColEval:
                return QStringLiteral("Eval");
            case ColLastUpdate:
                return QStringLiteral("Last update");
            default:
                break;
        }
    }
    return QAbstractTableModel::headerData(section, orientation, role);
}

std::optional<Job> JobsModel::jobAtRow(int row) const {
    if (row < 0 || row >= static_cast<int>(jobs_.size())) {
        return std::nullopt;
    }
    return jobs_[row];
}

} // namespace sf::client::ui
