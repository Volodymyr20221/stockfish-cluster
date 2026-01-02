#include "app/JobManager.hpp"

#include "app/JobSnapshotMerger.hpp"

#include <algorithm>
#include <chrono>

#include "app/ServerManager.hpp"
#include "app/IHistoryRepository.hpp"

namespace sf::client::app {

using namespace sf::client::domain;

JobManager::JobManager(ServerManager& serverManager,
                       sf::client::app::IHistoryRepository* historyRepo)
    : serverManager_(serverManager)
    , historyRepo_(historyRepo) {
}

JobId JobManager::makeJobId() {
    using namespace std::chrono;
    const auto ms = duration_cast<milliseconds>(Clock::now().time_since_epoch()).count();

    if (ms == lastIdMs_) {
        ++seqWithinMs_;
    } else {
        lastIdMs_ = ms;
        seqWithinMs_ = 0;
    }

    return "job-" + std::to_string(ms) + "-" + std::to_string(seqWithinMs_);
}

void JobManager::setCallbacks(JobManagerCallbacks callbacks) {
    callbacks_ = std::move(callbacks);
}

Job* JobManager::findJob(const JobId& id) {
    for (auto& j : jobs_) {
        if (j.id == id) {
            return &j;
        }
    }
    return nullptr;
}

const Job* JobManager::findJob(const JobId& id) const {
    for (const auto& j : jobs_) {
        if (j.id == id) {
            return &j;
        }
    }
    return nullptr;
}

JobId JobManager::enqueueJob(const std::string& opponent,
                             const std::string& fen,
                             const SearchLimit& limit,
                             int multiPv,
                             std::optional<std::string> preferredServer) {
    Job job;
    job.id           = makeJobId();
    job.opponent     = opponent;
    job.fen          = fen;
    job.limit        = limit;
    job.multiPv      = (multiPv < 1 ? 1 : multiPv);
    job.createdAt    = Clock::now();
    job.lastUpdateAt = job.createdAt;
    job.status       = JobStatus::Queued;

    // Pick server.
    if (auto* srv = serverManager_.pickServerForJob(preferredServer)) {
        job.assignedServer = srv->id;

        // Local optimistic load accounting (server_status will later correct this).
        srv->runtime.runningJobs++;
        if (srv->runtime.maxJobs <= 0) {
            srv->runtime.maxJobs = (srv->maxJobs > 0 ? srv->maxJobs : 0);
        }
        if (srv->runtime.maxJobs > 0) {
            srv->runtime.loadPercent =
                100.0 * static_cast<double>(srv->runtime.runningJobs) /
                static_cast<double>(srv->runtime.maxJobs);
        }
    } else {
        // No server available right now.
        job.status = JobStatus::Pending;
        job.logLines.push_back("No available server (Offline/Busy).");
    }

    jobs_.push_back(job);

    if (callbacks_.onJobAdded) {
        callbacks_.onJobAdded(jobs_.back());
    }

    return job.id;
}

void JobManager::persistIfTerminal(const Job& job) {
    switch (job.status) {
        case JobStatus::Finished:
        case JobStatus::Error:
        case JobStatus::Cancelled:
        case JobStatus::Stopped:
            if (historyRepo_) {
                historyRepo_->saveJob(job);
            }
            break;
        default:
            break;
    }
}

void JobManager::removeJobAtIndex(std::size_t index) {
    if (index >= jobs_.size()) {
        return;
    }

    Job jobCopy = jobs_[index]; // for callbacks and history

    // Update server load.
    if (jobCopy.assignedServer) {
        for (auto& s : serverManager_.servers()) {
            if (s.id == *jobCopy.assignedServer) {
                if (s.runtime.runningJobs > 0) {
                    s.runtime.runningJobs--;
                }
                const int maxJobs = (s.runtime.maxJobs > 0 ? s.runtime.maxJobs
                                                           : (s.maxJobs > 0 ? s.maxJobs : 0));
                if (maxJobs > 0) {
                    s.runtime.loadPercent =
                        100.0 * static_cast<double>(s.runtime.runningJobs) /
                        static_cast<double>(maxJobs);
                } else {
                    s.runtime.loadPercent = 0.0;
                }
            }
        }
    }

    persistIfTerminal(jobCopy);

    jobs_.erase(jobs_.begin() + static_cast<long long>(index));

    if (callbacks_.onJobRemoved) {
        callbacks_.onJobRemoved(jobCopy);
    }
}

void JobManager::requestStopJob(const JobId& id) {
    for (std::size_t i = 0; i < jobs_.size(); ++i) {
        if (jobs_[i].id == id) {
            jobs_[i].status       = JobStatus::Stopped;
            jobs_[i].finishedAt   = Clock::now();
            jobs_[i].lastUpdateAt = *jobs_[i].finishedAt;
            jobs_[i].logLines.push_back("Stopped by user.");
            persistIfTerminal(jobs_[i]);

            if (callbacks_.onJobUpdated) {
                callbacks_.onJobUpdated(jobs_[i]);
            }

            // Keep the job visible; network layer will send job_cancel based on Stopped status.
            return;
        }
    }
}

void JobManager::applyRemoteUpdate(const JobId& id,
                                   JobStatus status,
                                   const JobSnapshot& snapshot,
                                   std::optional<std::string> logLine) {
    for (std::size_t i = 0; i < jobs_.size(); ++i) {
        if (jobs_[i].id != id) {
            continue;
        }

        Job& job = jobs_[i];

        if (!job.startedAt && status == JobStatus::Running) {
            job.startedAt = Clock::now();
        }
        if ((status == JobStatus::Finished ||
             status == JobStatus::Error ||
             status == JobStatus::Cancelled) &&
            !job.finishedAt) {
            job.finishedAt = Clock::now();
        }

        job.status = status;

        // Keep all snapshot merging rules in one place.
        JobSnapshotMerger::merge(job.snapshot, snapshot);

        job.lastUpdateAt = Clock::now();

        if (logLine) {
            job.logLines.push_back(*logLine);
        }

        if (callbacks_.onJobUpdated) {
            callbacks_.onJobUpdated(job);
        }

        // Persist finished/failed/cancelled jobs but keep them visible in the UI.
        if (status == JobStatus::Finished ||
            status == JobStatus::Error ||
            status == JobStatus::Cancelled ||
            status == JobStatus::Stopped) {
            persistIfTerminal(job);
        }
        return;
    }
}

void JobManager::upsertRemoteJob(const sf::client::domain::Job& remote) {
    // If we already have this job, update in-place and notify UI.
    for (auto& job : jobs_) {
        if (job.id != remote.id) {
            continue;
        }

        job.opponent = remote.opponent;
        job.fen = remote.fen;
        job.limit = remote.limit;
        job.multiPv = remote.multiPv;
        job.status = remote.status;
        job.assignedServer = remote.assignedServer;
        job.createdAt = remote.createdAt;
        job.startedAt = remote.startedAt;
        job.finishedAt = remote.finishedAt;
        job.lastUpdateAt = remote.lastUpdateAt;
        job.snapshot = remote.snapshot;

        if (!remote.logLines.empty()) {
            // Replace only if remote has more info (e.g. server tail) or local is empty.
            if (job.logLines.empty() || remote.logLines.size() >= job.logLines.size()) {
                job.logLines = remote.logLines;
            }
        }

        if (callbacks_.onJobUpdated) {
            callbacks_.onJobUpdated(job);
        }

        if (job.status == JobStatus::Finished ||
            job.status == JobStatus::Error ||
            job.status == JobStatus::Cancelled ||
            job.status == JobStatus::Stopped) {
            persistIfTerminal(job);
        }
        return;
    }

    // New job discovered from server (likely after reconnect).
    jobs_.push_back(remote);
    if (callbacks_.onJobAdded) {
        callbacks_.onJobAdded(jobs_.back());
    }
    if (remote.status == JobStatus::Finished ||
        remote.status == JobStatus::Error ||
        remote.status == JobStatus::Cancelled ||
        remote.status == JobStatus::Stopped) {
        persistIfTerminal(jobs_.back());
    }
}

} // namespace sf::client::app
