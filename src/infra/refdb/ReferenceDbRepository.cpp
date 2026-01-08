#include "ReferenceDbRepository.hpp"

#include <QSqlError>
#include <QSqlQuery>

namespace sf::client::infra::refdb {

namespace {

bool execOrFail(QSqlQuery& q, const QString& sql, QString* err) {
    if (!q.exec(sql)) {
        if (err) *err = q.lastError().text() + " | SQL: " + sql;
        return false;
    }
    return true;
}

} // namespace

bool ReferenceDbRepository::createOrMigrate(QSqlDatabase& db, QString* errorOut) {
    if (!db.isValid() || !db.isOpen()) {
        if (errorOut) *errorOut = "ReferenceDbRepository: database is not open";
        return false;
    }

    QSqlQuery q(db);

    // Recommended pragmas for write-heavy import.
    // Safe defaults for desktop app. (WAL requires file-backed DB.)
    if (!execOrFail(q, "PRAGMA journal_mode=WAL;", errorOut)) return false;
    if (!execOrFail(q, "PRAGMA synchronous=NORMAL;", errorOut)) return false;
    if (!execOrFail(q, "PRAGMA temp_store=MEMORY;", errorOut)) return false;
    if (!execOrFail(q, "PRAGMA foreign_keys=ON;", errorOut)) return false;

    // Meta table
    if (!execOrFail(q, R"SQL(
        CREATE TABLE IF NOT EXISTS meta (
            key TEXT PRIMARY KEY,
            value TEXT NOT NULL
        );
    )SQL", errorOut)) return false;

    // Track the PGN source file for which this index was built.
    if (!execOrFail(q, R"SQL(
        CREATE TABLE IF NOT EXISTS source_files (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            path TEXT NOT NULL,
            size_bytes INTEGER NOT NULL,
            mtime_unix INTEGER NOT NULL,
            created_at_unix INTEGER NOT NULL
        );
    )SQL", errorOut)) return false;

    // Basic game headers + raw PGN slice offsets.
    if (!execOrFail(q, R"SQL(
        CREATE TABLE IF NOT EXISTS games (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            source_file_id INTEGER NOT NULL,
            offset_start INTEGER NOT NULL,
            offset_end INTEGER NOT NULL,

            white TEXT,
            black TEXT,
            white_elo INTEGER,
            black_elo INTEGER,
            result TEXT,       -- "1-0", "0-1", "1/2-1/2", "*"
            date_int INTEGER,  -- YYYYMMDD, or YYYY0000, or 0 if unknown
            year INTEGER,      -- YYYY or 0

            tags_json TEXT,    -- optional: store the full tag map if needed
            FOREIGN KEY(source_file_id) REFERENCES source_files(id)
        );
    )SQL", errorOut)) return false;

    if (!execOrFail(q, "CREATE INDEX IF NOT EXISTS idx_games_source_year ON games(source_file_id, year);", errorOut)) return false;
    if (!execOrFail(q, "CREATE INDEX IF NOT EXISTS idx_games_white ON games(white);", errorOut)) return false;
    if (!execOrFail(q, "CREATE INDEX IF NOT EXISTS idx_games_black ON games(black);", errorOut)) return false;

    // Aggregated stats by (position hash, move uci).
    // W/D/L are from the perspective of the side to move in the position.
    if (!execOrFail(q, R"SQL(
        CREATE TABLE IF NOT EXISTS move_agg (
            pos_hash INTEGER NOT NULL,
            move_uci TEXT NOT NULL,

            games INTEGER NOT NULL DEFAULT 0,
            w INTEGER NOT NULL DEFAULT 0,
            d INTEGER NOT NULL DEFAULT 0,
            l INTEGER NOT NULL DEFAULT 0,

            year_min INTEGER NOT NULL DEFAULT 0,
            year_max INTEGER NOT NULL DEFAULT 0,
            last_date_int INTEGER NOT NULL DEFAULT 0,

            PRIMARY KEY(pos_hash, move_uci)
        );
    )SQL", errorOut)) return false;

    if (!execOrFail(q, "CREATE INDEX IF NOT EXISTS idx_move_agg_pos ON move_agg(pos_hash);", errorOut)) return false;

    // Occurrences of positions in games (to build the lower 'Games' list fast).
    if (!execOrFail(q, R"SQL(
        CREATE TABLE IF NOT EXISTS occurrences (
            pos_hash INTEGER NOT NULL,
            game_id INTEGER NOT NULL,
            ply INTEGER NOT NULL,
            move_uci TEXT NOT NULL,
            PRIMARY KEY(pos_hash, game_id, ply),
            FOREIGN KEY(game_id) REFERENCES games(id)
        );
    )SQL", errorOut)) return false;

    if (!execOrFail(q, "CREATE INDEX IF NOT EXISTS idx_occ_pos ON occurrences(pos_hash);", errorOut)) return false;
    if (!execOrFail(q, "CREATE INDEX IF NOT EXISTS idx_occ_pos_move ON occurrences(pos_hash, move_uci);", errorOut)) return false;

    // Bump schema version in meta for future migrations.
    if (!execOrFail(q, "INSERT OR REPLACE INTO meta(key, value) VALUES('schema_version', '1');", errorOut)) return false;

    return true;
}

} // namespace sf::client::infra::refdb
