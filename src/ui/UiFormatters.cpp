#include "ui/UiFormatters.hpp"

#include <QDateTime>
#include <QTimeZone>
#include <chrono>

namespace sf::client::ui::fmt {

qint64 toUnixMs(sf::client::domain::TimePoint tp) {
    return static_cast<qint64>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            tp.time_since_epoch())
            .count());
}

QString formatLocalIso(sf::client::domain::TimePoint tp) {
    const auto ms = toUnixMs(tp);
    const QDateTime dt =
        QDateTime::fromMSecsSinceEpoch(ms, QTimeZone::utc());
    return dt.toLocalTime().toString(Qt::ISODate);
}

QString formatJobStatus(sf::client::domain::JobStatus status) {
    return QString::fromStdString(sf::client::domain::to_string(status));
}

QString formatScore(const sf::client::domain::Score& score) {
    return QString::fromStdString(sf::client::domain::to_string(score));
}

} // namespace sf::client::ui::fmt
