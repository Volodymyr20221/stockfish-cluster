#pragma once

#include <vector>

#include "domain/domain_model.hpp"

namespace sf::client::app {

// Port/interface for reading/writing server definitions.
// Implementations live in infra (e.g. JSON file).
class IServerConfigRepository {
public:
    virtual ~IServerConfigRepository() = default;

    virtual std::vector<sf::client::domain::ServerInfo> load() const = 0;
    virtual void save(const std::vector<sf::client::domain::ServerInfo>& servers) const = 0;
};

} // namespace sf::client::app
