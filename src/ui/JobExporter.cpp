#include "ui/JobExporter.hpp"
#include "ui/UiFormatters.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QTimeZone>

namespace sf::client::ui {

using sf::client::domain::Job;

namespace {

QJsonObject jobToJsonObject(const Job& job) {
    QJsonObject o;
    o.insert(QStringLiteral("id"), QString::fromStdString(job.id));
    o.insert(QStringLiteral("opponent"), QString::fromStdString(job.opponent));
    o.insert(QStringLiteral("fen"), QString::fromStdString(job.fen));
    o.insert(QStringLiteral("limit_type"), static_cast<int>(job.limit.type));
    o.insert(QStringLiteral("limit_value"), job.limit.value);
    o.insert(QStringLiteral("status"), static_cast<int>(job.status));

    if (job.assignedServer) {
        o.insert(QStringLiteral("server_id"), QString::fromStdString(*job.assignedServer));
    }

    o.insert(QStringLiteral("created_at_ms"), fmt::toUnixMs(job.createdAt));
    if (job.startedAt)  o.insert(QStringLiteral("started_at_ms"), fmt::toUnixMs(*job.startedAt));
    if (job.finishedAt) o.insert(QStringLiteral("finished_at_ms"), fmt::toUnixMs(*job.finishedAt));

    // snapshot
    QJsonObject snap;
    if (job.snapshot.depth)    snap.insert(QStringLiteral("depth"), *job.snapshot.depth);
    if (job.snapshot.selDepth) snap.insert(QStringLiteral("seldepth"), *job.snapshot.selDepth);

    if (job.snapshot.score.type == sf::client::domain::ScoreType::Cp) {
        snap.insert(QStringLiteral("score_cp"), job.snapshot.score.value);
    } else if (job.snapshot.score.type == sf::client::domain::ScoreType::Mate) {
        snap.insert(QStringLiteral("score_mate"), job.snapshot.score.value);
    }

    if (job.snapshot.nodes) snap.insert(QStringLiteral("nodes"), static_cast<qint64>(*job.snapshot.nodes));
    if (job.snapshot.nps)   snap.insert(QStringLiteral("nps"), static_cast<qint64>(*job.snapshot.nps));

    if (!job.snapshot.bestMove.empty()) {
        snap.insert(QStringLiteral("bestmove"), QString::fromStdString(job.snapshot.bestMove));
    }
    if (!job.snapshot.pv.empty()) {
        snap.insert(QStringLiteral("pv"), QString::fromStdString(job.snapshot.pv));
    }
    o.insert(QStringLiteral("snapshot"), snap);

    // log
    QJsonArray logArr;
    for (const auto& line : job.logLines) {
        logArr.append(QString::fromStdString(line));
    }
    o.insert(QStringLiteral("log"), logArr);

    return o;
}

void appendJobPgn(QTextStream& out, const Job& job) {
    out << "[Event \"CorrChess analysis\"]\n";
    out << "[Site \"Stockfish cluster\"]\n";
    out << "[White \"You\"]\n";
    out << "[Black \"" << QString::fromStdString(job.opponent) << "\"]\n";
    out << "[FEN \"" << QString::fromStdString(job.fen) << "\"]\n";
    out << "[Result \"*\"]\n";

    if (job.finishedAt) {
        const auto ms = fmt::toUnixMs(*job.finishedAt);
        const auto dt = QDateTime::fromMSecsSinceEpoch(ms, QTimeZone::utc()).toLocalTime();
        out << "[Date \"" << dt.date().toString(QStringLiteral("yyyy.MM.dd")) << "\"]\n";
    }

    out << "\n";

    out << "1. * { Stockfish: ";
    const auto scoreStr = sf::client::domain::to_string(job.snapshot.score);
    out << (scoreStr.empty() ? QStringLiteral("?") : QString::fromStdString(scoreStr));
    out << ", depth ";
    out << (job.snapshot.depth ? QString::number(*job.snapshot.depth) : QStringLiteral("?"));

    if (!job.snapshot.bestMove.empty()) {
        out << ", bestmove " << QString::fromStdString(job.snapshot.bestMove);
    }
    if (!job.snapshot.pv.empty()) {
        out << ", pv " << QString::fromStdString(job.snapshot.pv);
    }
    out << " }\n\n";
}

} // namespace

QJsonDocument JobExporter::toJson(const std::vector<Job>& jobs) {
    QJsonArray arr;
    for (const auto& job : jobs) {
        arr.append(jobToJsonObject(job));
    }
    return QJsonDocument(arr);
}

QString JobExporter::toPgn(const std::vector<Job>& jobs) {
    QString outStr;
    QTextStream out(&outStr);
    for (const auto& job : jobs) {
        appendJobPgn(out, job);
    }
    return outStr;
}

} // namespace sf::client::ui
