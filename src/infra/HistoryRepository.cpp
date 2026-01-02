#include "infra/HistoryRepository.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QVariant>
#include <QtSql/QSqlError>
#include <QtSql/QSqlQuery>
#include <QDebug>
#include <chrono>

namespace sf::client::infra {

using sf::client::domain::Clock;
using sf::client::domain::Job;
using sf::client::domain::JobSnapshot;
using sf::client::domain::LimitType;
using sf::client::domain::ScoreType;
using sf::client::domain::TimePoint;

namespace {

constexpr auto kConnName = "history";

qint64 toUnixMs(TimePoint tp) {
    return static_cast<qint64>(
        std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count());
}

QVariant toUnixMsOrNull(const std::optional<TimePoint>& tp) {
    if (!tp.has_value()) {
        return {};
    }
    return QVariant(toUnixMs(*tp));
}

TimePoint fromUnixMs(qint64 ms) {
    return TimePoint(std::chrono::milliseconds(ms));
}

QString snapshotToJson(const JobSnapshot& s) {
    QJsonObject o;
    if (s.depth)    o.insert(QStringLiteral("depth"), *s.depth);
    if (s.selDepth) o.insert(QStringLiteral("seldepth"), *s.selDepth);

    if (s.score.type == ScoreType::Cp) {
        o.insert(QStringLiteral("score_cp"), s.score.value);
    } else if (s.score.type == ScoreType::Mate) {
        o.insert(QStringLiteral("score_mate"), s.score.value);
    }

    if (s.nodes) o.insert(QStringLiteral("nodes"), static_cast<qint64>(*s.nodes));
    if (s.nps)   o.insert(QStringLiteral("nps"), static_cast<qint64>(*s.nps));

    if (!s.bestMove.empty()) o.insert(QStringLiteral("bestmove"), QString::fromStdString(s.bestMove));
    if (!s.pv.empty())       o.insert(QStringLiteral("pv"), QString::fromStdString(s.pv));

    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

JobSnapshot snapshotFromJson(const QString& json) {
    JobSnapshot s;
    if (json.trimmed().isEmpty()) {
        return s;
    }

    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(json.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return s;
    }

    const auto o = doc.object();
    if (o.contains(QStringLiteral("depth")))    s.depth    = o.value(QStringLiteral("depth")).toInt();
    if (o.contains(QStringLiteral("seldepth"))) s.selDepth = o.value(QStringLiteral("seldepth")).toInt();

    if (o.contains(QStringLiteral("score_cp"))) {
        s.score.type = ScoreType::Cp;
        s.score.value = o.value(QStringLiteral("score_cp")).toInt();
    } else if (o.contains(QStringLiteral("score_mate"))) {
        s.score.type = ScoreType::Mate;
        s.score.value = o.value(QStringLiteral("score_mate")).toInt();
    }

    if (o.contains(QStringLiteral("nodes"))) s.nodes = static_cast<std::int64_t>(o.value(QStringLiteral("nodes")).toVariant().toLongLong());
    if (o.contains(QStringLiteral("nps")))   s.nps   = static_cast<std::int64_t>(o.value(QStringLiteral("nps")).toVariant().toLongLong());

    if (o.contains(QStringLiteral("bestmove"))) s.bestMove = o.value(QStringLiteral("bestmove")).toString().toStdString();
    if (o.contains(QStringLiteral("pv")))       s.pv       = o.value(QStringLiteral("pv")).toString().toStdString();

    return s;
}

} // namespace

HistoryRepository::HistoryRepository(const QString& dbPath) {
    const auto connName = QStringLiteral("history");

    if (QSqlDatabase::contains(connName)) {
        db_ = QSqlDatabase::database(connName);
    } else {
        db_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db_.setDatabaseName(dbPath);
    }

    if (!db_.open()) {
        qWarning() << "Failed to open history DB:" << db_.lastError().text();
        return;
    }

    initSchema();
}

void HistoryRepository::initSchema() const {
    if (!db_.isOpen()) {
        return;
    }

    QSqlQuery q(db_);
    q.exec(
        "CREATE TABLE IF NOT EXISTS jobs ("
        "id TEXT PRIMARY KEY,"
        "opponent TEXT,"
        "fen TEXT,"
        "limit_type INTEGER,"
        "limit_value INTEGER,"
        "server_id TEXT,"
        "status INTEGER,"
        "created_at INTEGER,"
        "started_at INTEGER,"
        "finished_at INTEGER,"
        "result_json TEXT)");

    q.exec(
        "CREATE TABLE IF NOT EXISTS job_logs ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "job_id TEXT,"
        "ts INTEGER,"
        "line TEXT)");
}

static bool saveJobRow(QSqlDatabase& db, const Job& job) {
    QSqlQuery q(db);
    q.prepare(
        "INSERT OR REPLACE INTO jobs "
        "(id, opponent, fen, limit_type, limit_value, server_id, status,"
        " created_at, started_at, finished_at, result_json)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");

    q.addBindValue(QString::fromStdString(job.id));
    q.addBindValue(QString::fromStdString(job.opponent));
    q.addBindValue(QString::fromStdString(job.fen));
    q.addBindValue(static_cast<int>(job.limit.type));
    q.addBindValue(job.limit.value);

    if (job.assignedServer) {
        q.addBindValue(QString::fromStdString(*job.assignedServer));
    } else {
        q.addBindValue(QVariant());
    }

    q.addBindValue(static_cast<int>(job.status));
    q.addBindValue(QVariant(toUnixMs(job.createdAt)));
    q.addBindValue(toUnixMsOrNull(job.startedAt));
    q.addBindValue(toUnixMsOrNull(job.finishedAt));
    q.addBindValue(snapshotToJson(job.snapshot));

    if (!q.exec()) {
        qWarning() << "Failed to save job:" << q.lastError().text();
        return false;
    }
    return true;
}

static void replaceJobLogs(QSqlDatabase& db, const Job& job) {
    // Remove old logs
    {
        QSqlQuery qDel(db);
        qDel.prepare("DELETE FROM job_logs WHERE job_id = ?");
        qDel.addBindValue(QString::fromStdString(job.id));
        if (!qDel.exec()) {
            qWarning() << "Failed to clear job logs:" << qDel.lastError().text();
            return;
        }
    }

    if (job.logLines.empty()) {
        return;
    }

    QSqlQuery qIns(db);
    qIns.prepare("INSERT INTO job_logs (job_id, ts, line) VALUES (?, ?, ?)");

    const auto baseTp = job.finishedAt.value_or(job.createdAt);
    const qint64 baseMs = toUnixMs(baseTp);

    for (const auto& line : job.logLines) {
        qIns.addBindValue(QString::fromStdString(job.id));
        qIns.addBindValue(QVariant(baseMs));
        qIns.addBindValue(QString::fromStdString(line));
        if (!qIns.exec()) {
            qWarning() << "Failed to save job log:" << qIns.lastError().text();
            break;
        }
        qIns.finish();
    }
}

void HistoryRepository::saveJob(const Job& job) {
    if (!db_.isOpen()) {
        return;
    }

    if (!saveJobRow(db_, job)) {
        return;
    }

    replaceJobLogs(db_, job);
}

static void loadLogsIntoJob(const QSqlDatabase& db, Job& job) {
    QSqlQuery q(db);
    q.prepare("SELECT line FROM job_logs WHERE job_id = ? ORDER BY id ASC");
    q.addBindValue(QString::fromStdString(job.id));

    if (!q.exec()) {
        qWarning() << "Failed to read job logs:" << q.lastError().text();
        return;
    }

    while (q.next()) {
        job.logLines.push_back(q.value(0).toString().toStdString());
    }
}

std::vector<Job> HistoryRepository::loadAllJobs() const {
    std::vector<Job> out;

    if (!db_.isOpen()) {
        return out;
    }

    QSqlQuery q(db_);
    q.prepare(
        "SELECT id, opponent, fen, limit_type, limit_value, server_id, status,"
        " created_at, started_at, finished_at, result_json"
        " FROM jobs ORDER BY created_at DESC");

    if (!q.exec()) {
        qWarning() << "Failed to load jobs:" << q.lastError().text();
        return out;
    }

    while (q.next()) {
        Job job;

        job.id = q.value(0).toString().toStdString();
        job.opponent = q.value(1).toString().toStdString();
        job.fen = q.value(2).toString().toStdString();

        job.limit.type  = static_cast<LimitType>(q.value(3).toInt());
        job.limit.value = q.value(4).toInt();

        const auto serverId = q.value(5).toString();
        if (!serverId.isEmpty()) {
            job.assignedServer = serverId.toStdString();
        }

        job.status = static_cast<sf::client::domain::JobStatus>(q.value(6).toInt());

        const qint64 createdMs = q.value(7).toLongLong();
        job.createdAt = fromUnixMs(createdMs);

        if (!q.value(8).isNull()) {
            job.startedAt = fromUnixMs(q.value(8).toLongLong());
        }
        if (!q.value(9).isNull()) {
            job.finishedAt = fromUnixMs(q.value(9).toLongLong());
        }

        job.snapshot = snapshotFromJson(q.value(10).toString());

        // Last update: finished > started > created
        if (job.finishedAt) {
            job.lastUpdateAt = *job.finishedAt;
        } else if (job.startedAt) {
            job.lastUpdateAt = *job.startedAt;
        } else {
            job.lastUpdateAt = job.createdAt;
        }

        loadLogsIntoJob(db_, job);
        out.push_back(std::move(job));
    }

    return out;
}

} // namespace sf::client::infra
