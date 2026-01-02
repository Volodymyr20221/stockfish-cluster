#pragma once

#include <optional>
#include <string>
#include <vector>

#include "domain/domain_model.hpp"

namespace sf::client::app {

class ServerManager {
public:
    explicit ServerManager(std::vector<sf::client::domain::ServerInfo> servers);

    const std::vector<sf::client::domain::ServerInfo>& servers() const noexcept {
        return servers_;
    }
    std::vector<sf::client::domain::ServerInfo>& servers() noexcept {
        return servers_;
    }

    // Choose the best server for a job.
    // - If preferredId is set and server is available -> return it.
    // - Otherwise pick minimal load among Online servers.
    // - If no Online servers, fall back to Unknown servers.
    // - If none available -> nullptr.
    sf::client::domain::ServerInfo* pickServerForJob(
        const std::optional<std::string>& preferredId);

    // Update runtime and (optionally) hardware info from server_status.
    void updateServerRuntime(const std::string& id,
                             sf::client::domain::ServerStatus status,
                             int runningJobs,
                             int maxJobs,
                             int threadsPerJob,
                             int logicalCores);

private:
    sf::client::domain::ServerInfo* findServer(const std::string& id);
    static bool isAvailable(const sf::client::domain::ServerInfo& s);
    static double computeLoad(const sf::client::domain::ServerInfo& s);

    std::vector<sf::client::domain::ServerInfo> servers_;
};

} // namespace sf::client::app
