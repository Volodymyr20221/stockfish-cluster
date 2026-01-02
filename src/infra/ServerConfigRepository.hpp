#pragma once

#include <string>
#include <vector>

#include "domain/domain_model.hpp"
#include "app/IServerConfigRepository.hpp"

namespace sf::client::infra {

class ServerConfigRepository : public sf::client::app::IServerConfigRepository {
public:
    explicit ServerConfigRepository(std::string path);

    // Load server definitions from JSON file.
    // If file is missing or invalid, returns a single default server
    // (127.0.0.1:9000) and logs a warning.
    std::vector<sf::client::domain::ServerInfo> load() const override;

    // Save current server list back to JSON (for future editing UI).
    void save(const std::vector<sf::client::domain::ServerInfo>& servers) const override;

private:
    std::vector<sf::client::domain::ServerInfo> defaultServers() const;

    std::string path_;
};

} // namespace sf::client::infra
