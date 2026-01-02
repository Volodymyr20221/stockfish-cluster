#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sf::client::domain {

using Clock     = std::chrono::system_clock;
using TimePoint = Clock::time_point;
using JobId     = std::string;

// --- Search limit -----------------------------------------------------------

enum class LimitType {
    Depth  = 0,
    TimeMs = 1,
    Nodes  = 2
};

struct SearchLimit {
    LimitType type{LimitType::Depth};
    int       value{30}; // depth (plies) / time (ms) / nodes
};

inline SearchLimit depth(int d) {
    return {LimitType::Depth, d};
}

inline SearchLimit movetime_ms(int ms) {
    return {LimitType::TimeMs, ms};
}

inline SearchLimit nodes(int n) {
    return {LimitType::Nodes, n};
}

// --- Score & snapshot -------------------------------------------------------

enum class ScoreType {
    None = 0,
    Cp   = 1,
    Mate = 2
};

struct Score {
    ScoreType type{ScoreType::None};
    int       value{0}; // centipawns or mate in N
};

struct PvLine {
    int                    multipv{1};
    std::optional<int>     depth;
    std::optional<int>     selDepth;
    Score                  score;
    std::optional<int64_t> nodes;
    std::optional<int64_t> nps;
    std::string            pv;
};


struct JobSnapshot {
    std::optional<int>      depth;
    std::optional<int>      selDepth;
    Score                  score;
    std::optional<int64_t>  nodes;
    std::optional<int64_t>  nps;
    std::string            bestMove;
    std::string            pv;

    // MultiPV support: per-line PVs keyed by 'multipv' (1..N).
    std::vector<PvLine>     lines;
};

// --- Job status -------------------------------------------------------------

enum class JobStatus {
    Pending   = 0,
    Queued    = 1,
    Running   = 2,
    Finished  = 3,
    Error     = 4,
    Cancelled = 5,
    Stopped   = 6
};

// --- Job --------------------------------------------------------------------

struct Job {
    JobId                      id;
    std::string                opponent;
    std::string                fen;
    SearchLimit                limit;
    int                        multiPv{1}; // requested MultiPV (1..N)
    JobStatus                  status{JobStatus::Pending};
    std::optional<std::string> assignedServer;

    TimePoint                 createdAt{Clock::now()};
    std::optional<TimePoint>  startedAt;
    std::optional<TimePoint>  finishedAt;

    // For UI: shows "Last update" column.
    TimePoint lastUpdateAt{Clock::now()};

    JobSnapshot               snapshot;
    std::vector<std::string>  logLines;
};

// --- Servers ----------------------------------------------------------------

enum class ServerStatus {
    Unknown  = 0,
    Online   = 1,
    Degraded = 2, // UI shows as Busy
    Offline  = 3
};

struct ServerRuntimeState {
    ServerStatus status{ServerStatus::Unknown};
    int          runningJobs{0};
    int          maxJobs{0};
    double       loadPercent{0.0};
    TimePoint    lastSeen{Clock::now()};
};

struct ServerInfo {
    std::string id;
    std::string name;
    std::string host;
    int         port{0};

    // Static/cfg values (may be overwritten by server_status if provided)
    int cores{0};
    int threadsPerJob{1};
    int maxJobs{1};

    bool enabled{true};

    // TLS (optional): if enabled, client connects via TLS.
    // For mTLS, provide CA for server verification and client cert/key for client authentication.
    // Paths may be absolute or relative to application directory.
    bool        tlsEnabled{false};
    std::string tlsServerName;      // SNI / expected name (optional; defaults to host)
    std::string tlsCaFile;          // CA certificate (PEM)
    std::string tlsClientCertFile;  // client certificate (PEM)
    std::string tlsClientKeyFile;   // client private key (PEM)

    ServerRuntimeState runtime;
};

// --- Helpers ----------------------------------------------------------------

inline std::string to_string(JobStatus s) {
    switch (s) {
        case JobStatus::Pending:   return "Pending";
        case JobStatus::Queued:    return "Queued";
        case JobStatus::Running:   return "Running";
        case JobStatus::Finished:  return "Finished";
        case JobStatus::Error:     return "Error";
        case JobStatus::Cancelled: return "Cancelled";
        case JobStatus::Stopped:   return "Stopped";
    }
    return "Unknown";
}

inline std::string to_string(const Score& s) {
    switch (s.type) {
        case ScoreType::None:
            return "";
        case ScoreType::Cp:
            return std::to_string(s.value) + " cp";
        case ScoreType::Mate:
            return "M" + std::to_string(s.value);
    }
    return "";
}

inline std::string to_string(ServerStatus s) {
    switch (s) {
        case ServerStatus::Unknown:  return "Unknown";
        case ServerStatus::Online:   return "Online";
        case ServerStatus::Degraded: return "Degraded";
        case ServerStatus::Offline:  return "Offline";
    }
    return "Unknown";
}

} // namespace sf::client::domain
