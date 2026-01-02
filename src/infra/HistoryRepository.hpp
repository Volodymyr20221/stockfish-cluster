#pragma once

#include <QString>
#include <QtSql/QSqlDatabase>
#include <vector>

#include "domain/domain_model.hpp"
#include "app/IHistoryRepository.hpp"

namespace sf::client::infra {

// Very simple history storage using SQLite.
// We store terminal jobs in table `jobs` and all log lines in `job_logs`.
class HistoryRepository : public sf::client::app::IHistoryRepository {
public:
    explicit HistoryRepository(const QString& dbPath);

    void saveJob(const sf::client::domain::Job& job) override;

    // Load all saved jobs (terminal history).
    std::vector<sf::client::domain::Job> loadAllJobs() const override;

private:
    void initSchema() const;

    QSqlDatabase db_;
};

} // namespace sf::client::infra
