#pragma once

#include <QString>

#include "domain/domain_model.hpp"

namespace sf::client::ui::fmt {

// Convert domain timepoint to milliseconds since Unix epoch.
qint64 toUnixMs(sf::client::domain::TimePoint tp);

// Format a domain timepoint as local ISO datetime (Qt::ISODate).
QString formatLocalIso(sf::client::domain::TimePoint tp);

// Small helpers for common UI string conversions.
QString formatJobStatus(sf::client::domain::JobStatus status);
QString formatScore(const sf::client::domain::Score& score);

} // namespace sf::client::ui::fmt
