#include "app/ServerManager.hpp"

#include <algorithm>

namespace sf::client::app {

using namespace sf::client::domain;

ServerManager::ServerManager(std::vector<ServerInfo> servers)
    : servers_(std::move(servers)) {
    for (auto& s : servers_) {
        s.runtime.status      = ServerStatus::Unknown;
        s.runtime.runningJobs = 0;
        s.runtime.maxJobs     = (s.maxJobs > 0 ? s.maxJobs : 0);
        s.runtime.loadPercent = 0.0;
        s.runtime.lastSeen    = Clock::now();
    }
}

ServerInfo* ServerManager::findServer(const std::string& id) {
    auto it = std::find_if(servers_.begin(), servers_.end(),
                           [&id](const ServerInfo& s) { return s.id == id; });
    return it == servers_.end() ? nullptr : &(*it);
}

bool ServerManager::isAvailable(const ServerInfo& s) {
    if (!s.enabled) {
        return false;
    }
    if (s.runtime.status == ServerStatus::Offline) {
        return false;
    }

    const int maxJobs = (s.runtime.maxJobs > 0 ? s.runtime.maxJobs
                                               : (s.maxJobs > 0 ? s.maxJobs : 0));
    if (maxJobs > 0 && s.runtime.runningJobs >= maxJobs) {
        return false;
    }
    return true;
}

double ServerManager::computeLoad(const ServerInfo& s) {
    const int maxJobs = (s.runtime.maxJobs > 0 ? s.runtime.maxJobs
                                               : (s.maxJobs > 0 ? s.maxJobs : 0));
    if (maxJobs <= 0) {
        return 0.0;
    }
    return static_cast<double>(s.runtime.runningJobs) / static_cast<double>(maxJobs);
}

ServerInfo* ServerManager::pickServerForJob(
    const std::optional<std::string>& preferredId) {
    if (preferredId) {
        if (auto* s = findServer(*preferredId)) {
            if (isAvailable(*s) &&
                (s->runtime.status == ServerStatus::Online ||
                 s->runtime.status == ServerStatus::Unknown)) {
                return s;
            }
        }
    }

    auto pickFrom = [this](ServerStatus wanted) -> ServerInfo* {
        ServerInfo* best = nullptr;
        double bestLoad = 1e9;
        for (auto& s : servers_) {
            if (!isAvailable(s)) {
                continue;
            }
            if (s.runtime.status != wanted) {
                continue;
            }
            const double load = computeLoad(s);
            if (!best || load < bestLoad) {
                best = &s;
                bestLoad = load;
            }
        }
        return best;
    };

    // Prefer Online servers.
    if (auto* best = pickFrom(ServerStatus::Online)) {
        return best;
    }
    // Fall back to Unknown (before first ping).
    if (auto* best = pickFrom(ServerStatus::Unknown)) {
        return best;
    }
    return nullptr;
}

void ServerManager::updateServerRuntime(const std::string& id,
                                        ServerStatus status,
                                        int runningJobs,
                                        int maxJobs,
                                        int threadsPerJob,
                                        int logicalCores) {
    if (auto* s = findServer(id)) {
        s->runtime.status      = status;
        s->runtime.runningJobs = std::max(0, runningJobs);

        if (maxJobs > 0) {
            s->runtime.maxJobs = maxJobs;
            s->maxJobs = maxJobs;
        } else if (s->maxJobs > 0) {
            s->runtime.maxJobs = s->maxJobs;
        }

        if (threadsPerJob > 0) {
            s->threadsPerJob = threadsPerJob;
        }
        if (logicalCores > 0) {
            s->cores = logicalCores;
        }

        if (s->runtime.maxJobs > 0) {
            s->runtime.loadPercent =
                100.0 * static_cast<double>(s->runtime.runningJobs) /
                static_cast<double>(s->runtime.maxJobs);
        } else {
            s->runtime.loadPercent = 0.0;
        }

        s->runtime.lastSeen = Clock::now();
    }
}

} // namespace sf::client::app
