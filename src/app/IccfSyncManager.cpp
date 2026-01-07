#include "app/IccfSyncManager.hpp"

#include <algorithm>

#include "infra/iccf/IccfXfccParser.hpp"

namespace sf::client::app {

IccfSyncManager::IccfSyncManager(QObject* parent)
    : QObject(parent)
    , soap_(this) {
    QObject::connect(&soap_, &sf::client::net::iccf::IccfXfccSoap::finished,
                     this, &IccfSyncManager::handleSoapFinished);

    pollTimer_.setSingleShot(false);
    QObject::connect(&pollTimer_, &QTimer::timeout, this, &IccfSyncManager::refreshNow);
}

void IccfSyncManager::setCredentials(Credentials c) {
    creds_ = std::move(c);
    if (!creds_.endpointUrl.isEmpty()) {
        soap_.setEndpointUrl(QUrl(creds_.endpointUrl));
    }
}

IccfSyncManager::Credentials IccfSyncManager::credentials() const {
    return creds_;
}

void IccfSyncManager::refreshNow() {
    if (busy_) {
        emit status(tr("ICCF: busy"));
        return;
    }
    if (creds_.username.trimmed().isEmpty() || creds_.password.isEmpty()) {
        emit error(tr("ICCF: username/password is empty"));
        return;
    }
    if (!creds_.endpointUrl.trimmed().isEmpty()) {
        soap_.setEndpointUrl(QUrl(creds_.endpointUrl.trimmed()));
    }

    busy_ = true;
    emit status(tr("ICCF: refreshing..."));

    const QByteArray env = sf::client::net::iccf::IccfXfccSoap::buildGetMyGamesEnvelope(
        creds_.username.trimmed(), creds_.password);

    soap_.post(sf::client::net::iccf::IccfXfccSoap::Operation::GetMyGames, env);
}

void IccfSyncManager::startPolling(int intervalMs) {
    if (intervalMs < 1000) intervalMs = 1000;
    pollTimer_.setInterval(intervalMs);
    pollTimer_.start();
    emit status(tr("ICCF: polling %1 ms").arg(intervalMs));
}

void IccfSyncManager::stopPolling() {
    pollTimer_.stop();
    emit status(tr("ICCF: polling stopped"));
}

bool IccfSyncManager::isPolling() const {
    return pollTimer_.isActive();
}

void IccfSyncManager::handleSoapFinished(sf::client::net::iccf::IccfXfccSoap::Operation op,
                                         bool ok,
                                         QByteArray payload,
                                         QString errorMessage) {
    busy_ = false;

    if (op != sf::client::net::iccf::IccfXfccSoap::Operation::GetMyGames) {
        return;
    }

    if (!ok) {
        emit error(tr("ICCF: request failed: %1").arg(errorMessage));
        return;
    }

    const auto parsed = sf::client::infra::iccf::parseGetMyGamesSoapResponse(payload);
    if (!parsed.ok) {
        emit error(tr("ICCF: parse error: %1").arg(parsed.error));
        return;
    }

    games_ = parsed.games;

    // Stable, user-friendly order: myTurn first, then by id.
    std::stable_sort(games_.begin(), games_.end(), [](const auto& a, const auto& b) {
        if (a.myTurn != b.myTurn) return a.myTurn > b.myTurn;
        return a.id < b.id;
    });

    emit status(tr("ICCF: %1 games").arg(games_.size()));
    emit gamesUpdated(games_);
}

} // namespace sf::client::app
