#pragma once

#include <vector>

#include "domain/domain_model.hpp"

namespace sf::client::app {

// Port/interface for persisting and loading terminal jobs history.
// Implementations live in infra (e.g. SQLite via QtSql).
class IHistoryRepository {
public:
    virtual ~IHistoryRepository() = default;

    virtual void saveJob(const sf::client::domain::Job& job) = 0;

    // Load all saved jobs (terminal history).
    virtual std::vector<sf::client::domain::Job> loadAllJobs() const = 0;
};

} // namespace sf::client::app
