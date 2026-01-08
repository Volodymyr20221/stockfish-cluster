#pragma once

#include <QSqlDatabase>
#include <QString>

namespace sf::client::infra::refdb {

// Sidecar SQLite index for a large PGN file (ChessBase-like reference database).
// This repository only contains schema creation / migration helpers.
class ReferenceDbRepository final {
public:
    static bool createOrMigrate(QSqlDatabase& db, QString* errorOut);
};

} // namespace sf::client::infra::refdb
