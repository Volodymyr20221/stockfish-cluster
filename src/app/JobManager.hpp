#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "domain/domain_model.hpp"

namespace sf::client::app {
class IHistoryRepository;
}
namespace sf::client::app {

class ServerManager;

struct JobManagerCallbacks {
    std::function<void(const sf::client::domain::Job&)> onJobAdded;
    std::function<void(const sf::client::domain::Job&)> onJobUpdated;
    std::function<void(const sf::client::domain::Job&)> onJobRemoved;
};

class JobManager {
public:
    JobManager(ServerManager& serverManager,
               sf::client::app::IHistoryRepository* historyRepo = nullptr);

    void setCallbacks(JobManagerCallbacks callbacks);

    const std::vector<sf::client::domain::Job>& jobs() const noexcept {
        return jobs_;
    }

    sf::client::domain::JobId enqueueJob(
        const std::string& opponent,
        const std::string& fen,
        const sf::client::domain::SearchLimit& limit,
        int multiPv,
        std::optional<std::string> preferredServer);

    void requestStopJob(const sf::client::domain::JobId& id);

    // Called from network layer when server reports progress or result.
    void applyRemoteUpdate(const sf::client::domain::JobId& id,
                           sf::client::domain::JobStatus status,
                           const sf::client::domain::JobSnapshot& snapshot,
                           std::optional<std::string> logLine);

    // Called from network layer when reconnecting: restore jobs that are
    // still running on the server (or finished while the client was offline).
    void upsertRemoteJob(const sf::client::domain::Job& remote);

    // Re-try assigning servers for Pending jobs (when capacity becomes available).
    // Safe to call often (e.g. after server_status updates).
    void tryDispatchPendingJobs();

private:
    sf::client::domain::JobId makeJobId();
    sf::client::domain::Job*       findJob(const sf::client::domain::JobId& id);
    const sf::client::domain::Job* findJob(const sf::client::domain::JobId& id) const;

    void removeJobAtIndex(std::size_t index);
    void persistIfTerminal(const sf::client::domain::Job& job);

    // Assign server to one pending job (if possible) and notify callbacks.
    bool tryDispatchOnePendingJob();

    ServerManager&                         serverManager_;
    sf::client::app::IHistoryRepository*  historyRepo_;
    std::vector<sf::client::domain::Job>   jobs_;
    JobManagerCallbacks                    callbacks_;
    // Unique job IDs even across client restarts.
    long long                              lastIdMs_{0};
    int                                    seqWithinMs_{0};
};

} // namespace sf::client::app