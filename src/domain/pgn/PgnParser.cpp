#include "domain/pgn/PgnParser.hpp"

#include <cctype>
#include <string_view>

namespace sf::client::domain::pgn {

namespace {

static inline bool isBlank(const std::string& s) {
    for (unsigned char c : s) {
        if (!std::isspace(c)) return false;
    }
    return true;
}

static inline std::string trim(std::string_view v) {
    size_t b = 0;
    while (b < v.size() && std::isspace(static_cast<unsigned char>(v[b]))) ++b;
    size_t e = v.size();
    while (e > b && std::isspace(static_cast<unsigned char>(v[e - 1]))) --e;
    return std::string(v.substr(b, e - b));
}

static inline std::string_view sv(const std::string& s) {
    return std::string_view(s.data(), s.size());
}

static inline std::string unescapePgnString(std::string_view v) {
    std::string out;
    out.reserve(v.size());
    for (size_t i = 0; i < v.size(); ++i) {
        const char c = v[i];
        if (c == '\\' && i + 1 < v.size()) {
            const char n = v[i + 1];
            if (n == '\\' || n == '"') {
                out.push_back(n);
                ++i;
                continue;
            }
        }
        out.push_back(c);
    }
    return out;
}

// Very small tag parser. Returns true on success.
static bool parseTagLine(const std::string& line, std::string& outKey, std::string& outVal) {
    // Expected: [Key "Value"]
    if (line.size() < 4) return false;
    if (line.front() != '[') return false;
    if (line.back() != ']') return false;

    std::string_view mid(line.data() + 1, line.size() - 2);
    // Find first whitespace separating key and value.
    size_t sp = 0;
    while (sp < mid.size() && !std::isspace(static_cast<unsigned char>(mid[sp]))) ++sp;
    if (sp == 0 || sp >= mid.size()) return false;

    outKey = std::string(mid.substr(0, sp));

    // Find first quote after key.
    size_t q1 = mid.find('"', sp);
    if (q1 == std::string_view::npos) return false;
    // Find closing quote (scan, respecting escapes).
    size_t q2 = q1 + 1;
    bool escaped = false;
    for (; q2 < mid.size(); ++q2) {
        const char c = mid[q2];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (c == '\\') {
            escaped = true;
            continue;
        }
        if (c == '"') break;
    }
    if (q2 >= mid.size()) return false;

    const std::string_view rawVal = mid.substr(q1 + 1, q2 - (q1 + 1));
    outVal = unescapePgnString(rawVal);
    return true;
}

static void finalizeGame(PgnGame& cur, std::vector<PgnGame>& outGames) {
    // Normalize movetext spacing.
    cur.movetext = trim(sv(cur.movetext));
    if (!cur.tags.empty() || !cur.movetext.empty()) {
        outGames.push_back(std::move(cur));
    }
    cur = PgnGame{};
}

} // namespace

PgnParseResult parsePgnText(const std::string& text, int maxGames) {
    PgnParseResult res;
    if (maxGames <= 0) {
        res.ok = true;
        return res;
    }

    PgnGame cur;
    cur.movetext.reserve(4096);

    // Line-by-line scanning.
    size_t i = 0;
    auto readLine = [&](std::string& out) -> bool {
        if (i >= text.size()) return false;
        size_t j = i;
        while (j < text.size() && text[j] != '\n' && text[j] != '\r') ++j;
        out.assign(text.data() + i, j - i);
        // consume newline(s)
        if (j < text.size() && text[j] == '\r') ++j;
        if (j < text.size() && text[j] == '\n') ++j;
        i = j;
        return true;
    };

    std::string line;
    while (readLine(line)) {
        // Strip UTF-8 BOM if it appears at the beginning.
        if (res.games.empty() && cur.tags.empty() && cur.movetext.empty() && line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xEF && static_cast<unsigned char>(line[1]) == 0xBB &&
            static_cast<unsigned char>(line[2]) == 0xBF) {
            line.erase(0, 3);
        }

        if (isBlank(line)) {
            // Blank line does not necessarily end a game; but it separates tags and movetext.
            continue;
        }

        if (!line.empty() && line.front() == '[') {
            // If we already accumulated movetext, a new tag implies a new game.
            if (!cur.movetext.empty()) {
                finalizeGame(cur, res.games);
                if (static_cast<int>(res.games.size()) >= maxGames) break;
            }

            std::string k, v;
            if (parseTagLine(line, k, v)) {
                cur.tags[k] = v;
            }
            continue;
        }

        // Movetext line.
        if (!cur.movetext.empty()) cur.movetext.push_back(' ');
        cur.movetext += trim(sv(line));
    }

    if (static_cast<int>(res.games.size()) < maxGames) {
        finalizeGame(cur, res.games);
    }

    if (res.games.empty()) {
        res.ok = false;
        res.error = "No PGN games found";
        return res;
    }

    res.ok = true;
    return res;
}

} // namespace sf::client::domain::pgn
