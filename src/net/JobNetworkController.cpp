#include "net/JobNetworkController.hpp"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>

#include <cstdint>
#include <chrono>

namespace sf::client::net {

using namespace sf::client::domain;

namespace {

static std::optional<std::chrono::system_clock::time_point> timeFromMs(const QJsonValue& v) {
    if (!v.isDouble()) {
        return std::nullopt;
    }
    const auto ms = static_cast<qint64>(v.toDouble());
    if (ms <= 0) {
        return std::nullopt;
    }
    return std::chrono::system_clock::time_point{std::chrono::milliseconds(ms)};
}

static Score parseScore(const QJsonObject& obj) {
    Score s;
    if (obj.contains(QStringLiteral("score_cp"))) {
        s.type = ScoreType::Cp;
        s.value = obj.value(QStringLiteral("score_cp")).toInt(0);
    } else if (obj.contains(QStringLiteral("score_mate"))) {
        s.type = ScoreType::Mate;
        s.value = obj.value(QStringLiteral("score_mate")).toInt(0);
    }
    return s;
}

static std::vector<std::string> parseLogTail(const QJsonValue& v) {
    std::vector<std::string> out;
    if (!v.isArray()) {
        return out;
    }
    const auto arr = v.toArray();
    out.reserve(static_cast<size_t>(arr.size()));
    for (const auto& lv : arr) {
        const auto s = lv.toString();
        if (!s.isEmpty()) {
            out.push_back(s.toStdString());
        }
    }
    return out;
}

static std::vector<PvLine> parsePvLines(const QJsonValue& v) {
    std::vector<PvLine> out;
    if (!v.isArray()) {
        return out;
    }

    const auto arr = v.toArray();
    out.reserve(static_cast<size_t>(arr.size()));
    for (const auto& lval : arr) {
        if (!lval.isObject()) {
            continue;
        }
        const auto lo = lval.toObject();
        PvLine line;
        line.multipv = lo.value(QStringLiteral("multipv")).toInt(1);
        if (lo.contains(QStringLiteral("depth"))) {
            line.depth = lo.value(QStringLiteral("depth")).toInt();
        }
        if (lo.contains(QStringLiteral("seldepth"))) {
            line.selDepth = lo.value(QStringLiteral("seldepth")).toInt();
        }
        line.score = parseScore(lo);
        line.pv = lo.value(QStringLiteral("pv")).toString().toStdString();
        out.push_back(std::move(line));
    }

    std::sort(out.begin(), out.end(), [](const PvLine& a, const PvLine& b) { return a.multipv < b.multipv; });
    return out;
}

static JobSnapshot parseSnapshot(const QJsonValue& v) {
    JobSnapshot snap;
    if (!v.isObject()) {
        return snap;
    }

    const auto so = v.toObject();
    if (so.contains(QStringLiteral("depth"))) {
        snap.depth = so.value(QStringLiteral("depth")).toInt(0);
    }
    if (so.contains(QStringLiteral("seldepth"))) {
        snap.selDepth = so.value(QStringLiteral("seldepth")).toInt(0);
    }

    snap.score = parseScore(so);

    if (so.contains(QStringLiteral("nodes"))) {
        snap.nodes = static_cast<std::int64_t>(so.value(QStringLiteral("nodes")).toDouble(0));
    }
    if (so.contains(QStringLiteral("nps"))) {
        snap.nps = static_cast<std::int64_t>(so.value(QStringLiteral("nps")).toDouble(0));
    }

    snap.bestMove = so.value(QStringLiteral("bestmove")).toString().toStdString();
    snap.pv       = so.value(QStringLiteral("pv")).toString().toStdString();
    snap.lines    = parsePvLines(so.value(QStringLiteral("lines")));
    return snap;
}

static std::optional<Job> parseJobsListItem(const QJsonValue& jobVal, const QString& serverId) {
    if (!jobVal.isObject()) {
        return std::nullopt;
    }

    const auto jo = jobVal.toObject();
    Job job;
    job.id         = jo.value(QStringLiteral("id")).toString().toStdString();
    if (job.id.empty()) {
        return std::nullopt;
    }
    job.opponent   = jo.value(QStringLiteral("opponent")).toString().toStdString();
    job.fen        = jo.value(QStringLiteral("fen")).toString().toStdString();
    job.multiPv    = jo.value(QStringLiteral("multipv")).toInt(1);

    job.status = static_cast<JobStatus>(jo.value(QStringLiteral("status")).toInt(0));

    job.limit.type = static_cast<sf::client::domain::LimitType>(
        jo.value(QStringLiteral("limit_type"))
            .toInt(static_cast<int>(sf::client::domain::LimitType::Depth)));
    job.limit.value = jo.value(QStringLiteral("limit_value")).toInt(0);

    job.assignedServer = serverId.toStdString();
    if (const auto created = timeFromMs(jo.value(QStringLiteral("created_at_ms"))); created.has_value()) {
        job.createdAt = *created;
    }
    job.startedAt  = timeFromMs(jo.value(QStringLiteral("started_at_ms")));
    job.finishedAt = timeFromMs(jo.value(QStringLiteral("finished_at_ms")));
    job.snapshot = parseSnapshot(jo.value(QStringLiteral("snapshot")));
    job.logLines = parseLogTail(jo.value(QStringLiteral("log_tail")));
    return job;
}

} // namespace

JobNetworkController::JobNetworkController(sf::client::app::JobManager& jobManager,
                                           sf::client::app::ServerManager& serverManager,
                                           QObject* parent)
    : QObject(parent)
    , jobManager_(jobManager)
    , serverManager_(serverManager) {
    pingTimer_.setInterval(3000); // 3 seconds
    connect(&pingTimer_, &QTimer::timeout,
            this, &JobNetworkController::onPingTimeout);
    pingTimer_.start();
}

void JobNetworkController::initializeConnections(
    const std::vector<ServerInfo>& servers) {
    for (const auto& s : servers) {
        const std::string key = s.id;
        if (connections_.find(key) != connections_.end()) {
            continue;
        }

        auto conn = std::make_unique<JobConnection>(
            QString::fromStdString(s.id),
            QString::fromStdString(s.host),
            static_cast<quint16>(s.port),
            s.tlsEnabled,
            QString::fromStdString(s.tlsServerName),
            QString::fromStdString(s.tlsCaFile),
            QString::fromStdString(s.tlsClientCertFile),
            QString::fromStdString(s.tlsClientKeyFile),
            this);

        connect(conn.get(), &JobConnection::jsonReceived,
                this, &JobNetworkController::onJsonReceived);
        connect(conn.get(), &JobConnection::connectionReady,
                this, &JobNetworkController::onConnectionReady);
        connect(conn.get(), &JobConnection::disconnected,
                this, &JobNetworkController::onConnectionDisconnected);

        conn->connectToHost();
        connections_.emplace(key, std::move(conn));
    }
}

void JobNetworkController::handleJobAddedOrUpdated(const Job& job) {
    if (!job.assignedServer) {
        return;
    }
    auto it = connections_.find(*job.assignedServer);
    if (it == connections_.end()) {
        return;
    }

    QJsonObject jobObj;
    jobObj.insert(QStringLiteral("id"),         QString::fromStdString(job.id));
    jobObj.insert(QStringLiteral("opponent"),   QString::fromStdString(job.opponent));
    jobObj.insert(QStringLiteral("fen"),        QString::fromStdString(job.fen));
    jobObj.insert(QStringLiteral("limit_type"), static_cast<int>(job.limit.type));
    jobObj.insert(QStringLiteral("limit_value"), job.limit.value);
    jobObj.insert(QStringLiteral("multipv"), job.multiPv);

    QJsonObject msg;
    msg.insert(QStringLiteral("type"), QStringLiteral("job_submit_or_update"));
    msg.insert(QStringLiteral("job"), jobObj);

    it->second->sendJson(msg);
}

void JobNetworkController::handleJobRemoved(const Job& job) {
    if (!job.assignedServer) {
        return;
    }
    auto it = connections_.find(*job.assignedServer);
    if (it == connections_.end()) {
        return;
    }

    QJsonObject msg;
    msg.insert(QStringLiteral("type"), QStringLiteral("job_cancel"));
    msg.insert(QStringLiteral("job_id"), QString::fromStdString(job.id));
    it->second->sendJson(msg);
}

QString JobNetworkController::detectMessageType(const QJsonObject& obj) const {
    QString type = obj.value(QStringLiteral("type")).toString();

    // Compatibility: some older servers may omit {"type":"server_status"} and/or "server_id".
    if (type.isEmpty()) {
        const bool looksLikeStatus =
            obj.contains(QStringLiteral("status")) &&
            (obj.contains(QStringLiteral("running_jobs")) ||
             obj.contains(QStringLiteral("running")) ||
             obj.contains(QStringLiteral("max_jobs")) ||
             obj.contains(QStringLiteral("max")));
        if (looksLikeStatus) {
            type = QStringLiteral("server_status");
        }
    }

    return type;
}

void JobNetworkController::handleJobUpdateMessage(const QString& serverId,
                                                 const QJsonObject& obj) {
    Q_UNUSED(serverId);

    const auto jobId = obj.value(QStringLiteral("job_id")).toString().toStdString();
    const auto statusInt = obj.value(QStringLiteral("status"))
                               .toInt(static_cast<int>(JobStatus::Running));
    const auto status = static_cast<JobStatus>(statusInt);

    JobSnapshot snap;

    // NOTE about "depth jumps": Stockfish emits a lot of "info ... currmove ..." lines
    // (without score/pv). If we treat those as authoritative, UI depth starts to oscillate
    // (35 -> 34 -> 35 ...). We only update analysis snapshot from lines that carry
    // an actual evaluation (score and/or pv).

    const bool hasScore = obj.contains(QStringLiteral("score_cp")) || obj.contains(QStringLiteral("score_mate"));
    const bool hasPv    = obj.contains(QStringLiteral("pv")) && !obj.value(QStringLiteral("pv")).toString().isEmpty();
    const bool isEvalUpdate = hasScore || hasPv;

    if (isEvalUpdate) {
        // MultiPV: server may send updates for different 'multipv' lines.
        const int multipv = obj.value(QStringLiteral("multipv")).toInt(1);

        PvLine line;
        line.multipv = multipv;

        if (obj.contains(QStringLiteral("depth"))) {
            line.depth = obj.value(QStringLiteral("depth")).toInt();
        }
        if (obj.contains(QStringLiteral("seldepth"))) {
            line.selDepth = obj.value(QStringLiteral("seldepth")).toInt();
        }
        if (obj.contains(QStringLiteral("score_cp"))) {
            line.score.type  = ScoreType::Cp;
            line.score.value = obj.value(QStringLiteral("score_cp")).toInt();
        } else if (obj.contains(QStringLiteral("score_mate"))) {
            line.score.type  = ScoreType::Mate;
            line.score.value = obj.value(QStringLiteral("score_mate")).toInt();
        }
        if (obj.contains(QStringLiteral("nodes"))) {
            line.nodes = static_cast<int64_t>(obj.value(QStringLiteral("nodes")).toVariant().toLongLong());
        }
        if (obj.contains(QStringLiteral("nps"))) {
            line.nps = static_cast<int64_t>(obj.value(QStringLiteral("nps")).toVariant().toLongLong());
        }
        if (obj.contains(QStringLiteral("pv"))) {
            line.pv = obj.value(QStringLiteral("pv")).toString().toStdString();
        }

        // Preserve single-line fields for the UI (multipv=1 only).
        if (multipv == 1) {
            snap.depth    = line.depth;
            snap.selDepth = line.selDepth;
            snap.score    = line.score;
            snap.nodes    = line.nodes;
            snap.nps      = line.nps;
            snap.pv       = line.pv;
        }

        // Attach the per-line update.
        snap.lines.push_back(std::move(line));
    }

    if (obj.contains(QStringLiteral("bestmove"))) {
        snap.bestMove = obj.value(QStringLiteral("bestmove")).toString().toStdString();
    }

    std::optional<std::string> logLine;
    if (obj.contains(QStringLiteral("log_line"))) {
        const auto s = obj.value(QStringLiteral("log_line")).toString();
        if (!s.isEmpty()) {
            logLine = s.toStdString();
        }
    }

    jobManager_.applyRemoteUpdate(jobId, status, snap, logLine);
}

void JobNetworkController::handleServerStatusMessage(const QString& serverId,
                                                    const QJsonObject& obj) {
    // IMPORTANT: we map runtime updates to the connection/config id (serverId parameter),
    // not to the server-reported "server_id" field. This avoids tight coupling between
    // config ids and server runtime ids.
    const int statI = obj.value(QStringLiteral("status"))
                          .toInt(static_cast<int>(ServerStatus::Online));

    // Compatibility: accept older field names as well.
    const int running = obj.contains(QStringLiteral("running_jobs"))
        ? obj.value(QStringLiteral("running_jobs")).toInt(0)
        : obj.value(QStringLiteral("running")).toInt(0);

    int maxJobs = 0;
    if (obj.contains(QStringLiteral("max_jobs"))) {
        maxJobs = obj.value(QStringLiteral("max_jobs")).toInt(0);
    } else if (obj.contains(QStringLiteral("max"))) {
        maxJobs = obj.value(QStringLiteral("max")).toInt(0);
    }

    const int threadsPerJob = obj.value(QStringLiteral("threads")).toInt(0);
    const int logicalCores  = obj.value(QStringLiteral("logical_cores")).toInt(0);

    serverManager_.updateServerRuntime(
        serverId.toStdString(),
        static_cast<ServerStatus>(statI),
        running,
        maxJobs,
        threadsPerJob,
        logicalCores);
}

void JobNetworkController::onConnectionReady(const QString& serverId) {
    // Immediately sync jobs so that reconnect restores ongoing analysis.
    sendJobsListRequest(serverId);
}

void JobNetworkController::onJsonReceived(const QString& serverId,
                                         const QJsonObject& obj) {
    const QString type = detectMessageType(obj);

    if (type == QStringLiteral("job_update")) {
        handleJobUpdateMessage(serverId, obj);
        return;
    }

    if (type == QStringLiteral("server_status")) {
        handleServerStatusMessage(serverId, obj);
        return;
    }

    if (type == QStringLiteral("jobs_list")) {
        handleJobsListMessage(serverId, obj);
        return;
    }

    qDebug() << "Unknown message type:" << type << "payload:" << QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

void JobNetworkController::onConnectionDisconnected(
    const QString& serverId) {
    serverManager_.updateServerRuntime(
        serverId.toStdString(),
        ServerStatus::Offline,
        0,
        0,
        0,
        0);
}

void JobNetworkController::onPingTimeout() {
    sendPing();
}

void JobNetworkController::sendJobsListRequest(const QString& serverId) {
	const auto it = connections_.find(serverId.toStdString());
    if (it == connections_.end()) {
        return;
    }

    if (!it->second->isConnected()) {
        it->second->connectToHost();
        // We'll request again on connectionReady.
        return;
    }

    QJsonObject msg;
    msg.insert(QStringLiteral("type"), QStringLiteral("jobs_list"));
    msg.insert(QStringLiteral("include_finished"), true);
    msg.insert(QStringLiteral("limit"), 200);
    it->second->sendJson(msg);
}

void JobNetworkController::sendPing() {
    QJsonObject msg;
    msg.insert(QStringLiteral("type"), QStringLiteral("ping"));

    for (auto& [id, conn] : connections_) {
        if (!conn->isConnected()) {
            conn->connectToHost(); // best-effort reconnect
        }
        conn->sendJson(msg);
    }
}

void JobNetworkController::handleJobsListMessage(const QString& serverId,
                                                 const QJsonObject& obj) {
    const auto jobsVal = obj.value(QStringLiteral("jobs"));
    if (!jobsVal.isArray()) {
        return;
    }

    for (const auto& v : jobsVal.toArray()) {
        if (!v.isObject()) {
            continue;
        }
        auto job = parseJobsListItem(v.toObject(), serverId);
        if (!job.has_value()) {
            continue;
        }
        jobManager_.upsertRemoteJob(*job);
    }
}

} // namespace sf::client::net
