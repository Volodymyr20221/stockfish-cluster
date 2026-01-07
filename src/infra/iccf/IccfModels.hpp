#pragma once

#include <optional>

#include <QDate>
#include <QString>
#include <QVector>

namespace sf::client::infra::iccf {

// DTO for the XfccBasic GetMyGames result.
// Fields follow the XfccBasic spec; many are optional and may be absent.
struct IccfGame {
    int id = 0;

    QString white;
    QString black;
    QString event;
    QString site;

    bool myTurn = false;
    bool hasWhite = false;

    int daysPlayer = 0;
    int hoursPlayer = 0;
    int minutesPlayer = 0;

    int daysOpponent = 0;
    int hoursOpponent = 0;
    int minutesOpponent = 0;

    QString moves;        // PGN movetext (may be without line breaks)
    bool drawOffered = false;

    // Optional extras
    QString message;
    QString serverInfo;
    QString gameLink;

    // Optional PGN-derived fields
    QString whiteTitle;
    QString blackTitle;
    int whiteElo = 0;
    int blackElo = 0;
    QString whiteNA;
    QString blackNA;
    QString eventSponsor;
    QString section;
    QString stage;
    QString board;
    QString timeControl;
    QString variant;
    QString eventDate;

    bool setup = false;
    QString fen;
    QString result; // enum as string (e.g. Ongoing, WhiteWins, Draw ...)
};

struct ParseGetMyGamesResult {
    bool ok = false;
    QVector<IccfGame> games;
    QString error;
};

} // namespace sf::client::infra::iccf
