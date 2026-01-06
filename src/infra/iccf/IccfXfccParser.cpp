#include "infra/iccf/IccfXfccParser.hpp"

#include <QXmlStreamReader>

namespace sf::client::infra::iccf {
namespace {

inline QString localName(const QXmlStreamReader& x) {
    return x.name().toString();
}

inline QString readElementTextTrimmed(QXmlStreamReader& x) {
    return x.readElementText(QXmlStreamReader::IncludeChildElements).trimmed();
}

inline bool toBool(const QString& s) {
    const QString v = s.trimmed().toLower();
    return (v == QStringLiteral("true") || v == QStringLiteral("1"));
}

inline int toIntSafe(const QString& s) {
    bool ok = false;
    const int v = s.trimmed().toInt(&ok);
    return ok ? v : 0;
}

IccfGame parseGame(QXmlStreamReader& x) {
    IccfGame g;
    // <XfccGame> ... </XfccGame>
    while (x.readNextStartElement()) {
        const QString n = localName(x);
        const QString val = readElementTextTrimmed(x);

        if (n == QStringLiteral("id")) g.id = toIntSafe(val);
        else if (n == QStringLiteral("white")) g.white = val;
        else if (n == QStringLiteral("black")) g.black = val;
        else if (n == QStringLiteral("event")) g.event = val;
        else if (n == QStringLiteral("site")) g.site = val;
        else if (n == QStringLiteral("myTurn")) g.myTurn = toBool(val);
        else if (n == QStringLiteral("hasWhite")) g.hasWhite = toBool(val);

        else if (n == QStringLiteral("daysPlayer")) g.daysPlayer = toIntSafe(val);
        else if (n == QStringLiteral("hoursPlayer")) g.hoursPlayer = toIntSafe(val);
        else if (n == QStringLiteral("minutesPlayer")) g.minutesPlayer = toIntSafe(val);

        else if (n == QStringLiteral("daysOpponent")) g.daysOpponent = toIntSafe(val);
        else if (n == QStringLiteral("hoursOpponent")) g.hoursOpponent = toIntSafe(val);
        else if (n == QStringLiteral("minutesOpponent")) g.minutesOpponent = toIntSafe(val);

        else if (n == QStringLiteral("moves")) g.moves = val;
        else if (n == QStringLiteral("drawOffered")) g.drawOffered = toBool(val);

        else if (n == QStringLiteral("message")) g.message = val;
        else if (n == QStringLiteral("serverInfo")) g.serverInfo = val;
        else if (n == QStringLiteral("gameLink")) g.gameLink = val;

        else if (n == QStringLiteral("setup")) g.setup = toBool(val);
        else if (n == QStringLiteral("fen")) g.fen = val;
        else if (n == QStringLiteral("result")) g.result = val;
        else {
            // Ignore unknown fields (spec allows server-specific extensions).
        }
    }
    return g;
}

} // namespace

ParseGetMyGamesResult parseGetMyGamesSoapResponse(const QByteArray& xml) {
    ParseGetMyGamesResult res;

    QXmlStreamReader x(xml);
    while (!x.atEnd()) {
        x.readNext();
        if (!x.isStartElement()) continue;

        // Games are contained under <GetMyGamesResult> with repeated <XfccGame> items.
        if (localName(x) == QStringLiteral("XfccGame")) {
            res.games.push_back(parseGame(x));
        }
    }

    if (x.hasError()) {
        res.ok = false;
        res.error = x.errorString();
        res.games.clear();
        return res;
    }

    res.ok = true;
    return res;
}

} // namespace sf::client::infra::iccf
