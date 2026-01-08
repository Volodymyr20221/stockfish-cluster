#include "domain/pgn/PgnStreamScanner.hpp"

#include <cctype>
#include <fstream>
#include <string_view>

namespace sf::client::domain::pgn {

namespace {

static inline bool isBlankLine(const std::string& s) {
    for (unsigned char c : s) {
        if (!std::isspace(c)) return false;
    }
    return true;
}

static inline void rstripCr(std::string& s) {
    if (!s.empty() && s.back() == '\r') s.pop_back();
}

static inline std::string ltrimCopy(std::string_view s) {
    size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    return std::string(s.substr(i));
}

static bool parseTagLine(const std::string& line, std::string& outKey, std::string& outVal) {
    // Minimal PGN tag parser: [Key "Value"]  (supports \" escapes)
    if (line.size() < 4) return false;
    if (line.front() != '[' || line.back() != ']') return false;

    std::string_view mid(line.data() + 1, line.size() - 2);

    // key until first whitespace
    size_t sp = 0;
    while (sp < mid.size() && !std::isspace(static_cast<unsigned char>(mid[sp]))) ++sp;
    if (sp == 0 || sp >= mid.size()) return false;

    outKey = std::string(mid.substr(0, sp));

    // find first quote after key
    size_t q1 = mid.find('"', sp);
    if (q1 == std::string_view::npos) return false;

    // find closing quote, respecting escapes
    size_t q2 = q1 + 1;
    bool escaped = false;
    while (q2 < mid.size()) {
        const char c = mid[q2];
        if (escaped) {
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else if (c == '"') {
            break;
        }
        ++q2;
    }
    if (q2 >= mid.size() || mid[q2] != '"') return false;

    std::string raw(mid.substr(q1 + 1, q2 - (q1 + 1)));

    std::string unescaped;
    unescaped.reserve(raw.size());
    escaped = false;
    for (char c : raw) {
        if (escaped) {
            unescaped.push_back(c);
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else {
            unescaped.push_back(c);
        }
    }

    outVal = std::move(unescaped);
    return true;
}

static inline void appendMovetext(std::string& dst, const std::string& line) {
    if (dst.empty()) {
        dst = line;
    } else {
        dst.push_back(' ');
        dst += line;
    }
}

static inline void finalizeGame(PgnStreamGame& g) {
    // Trim trailing spaces in movetext.
    while (!g.movetext.empty() && std::isspace(static_cast<unsigned char>(g.movetext.back()))) {
        g.movetext.pop_back();
    }
}

} // namespace

PgnStreamScanResult scanPgnFile(
    const std::string& filePath,
    const std::function<bool(const PgnStreamGame&, std::uint64_t, std::string*)>& onGame,
    int maxGames) {

    PgnStreamScanResult res;

    std::ifstream in(filePath, std::ios::binary);
    if (!in.is_open()) {
        res.ok = false;
        res.error = "Cannot open PGN file";
        return res;
    }

    // file size
    in.seekg(0, std::ios::end);
    const std::uint64_t fileSize = static_cast<std::uint64_t>(in.tellg());
    in.seekg(0, std::ios::beg);

    PgnStreamGame cur;
    bool inGame = false;
    bool haveMovetext = false;

    std::string line;
    bool firstLine = true;

    std::uint64_t lastAfter = 0;

    while (true) {
        const auto startPos = in.tellg();
        if (startPos < 0) break;

        const std::uint64_t lineStart = static_cast<std::uint64_t>(startPos);

        if (!std::getline(in, line)) {
            break;
        }

        auto afterPos = in.tellg();
        if (afterPos < 0) {
            // likely EOF without trailing newline
            lastAfter = fileSize;
        } else {
            lastAfter = static_cast<std::uint64_t>(afterPos);
        }

        if (firstLine) {
            firstLine = false;
            // Strip UTF-8 BOM if present.
            if (line.size() >= 3 &&
                static_cast<unsigned char>(line[0]) == 0xEF &&
                static_cast<unsigned char>(line[1]) == 0xBB &&
                static_cast<unsigned char>(line[2]) == 0xBF) {
                line.erase(0, 3);
            }
        }

        rstripCr(line);

        if (isBlankLine(line)) {
            // Blank line: may separate tags and movetext. Ignore.
            res.bytesProcessed = lastAfter;
            continue;
        }

        const std::string trimmed = ltrimCopy(line);
        if (!trimmed.empty() && trimmed.front() == '[') {
            std::string key, val;
            if (!parseTagLine(trimmed, key, val)) {
                // Non-standard tag line; ignore.
                res.bytesProcessed = lastAfter;
                continue;
            }

            if (!inGame) {
                inGame = true;
                haveMovetext = false;
                cur = PgnStreamGame{};
                cur.offsetStart = lineStart;
            } else if (haveMovetext) {
                // New game's tag after movetext -> finalize current and emit it.
                cur.offsetEnd = lineStart;
                finalizeGame(cur);

                std::string cbErr;
                const bool cont = onGame(cur, res.bytesProcessed, &cbErr);
                if (!cbErr.empty()) {
                    res.ok = false;
                    res.error = cbErr;
                    return res;
                }
                ++res.games;
                if (!cont) {
                    res.ok = true;
                    return res;
                }
                if (maxGames > 0 && res.games >= maxGames) {
                    res.ok = true;
                    return res;
                }

                // Start new game with current line.
                inGame = true;
                haveMovetext = false;
                cur = PgnStreamGame{};
                cur.offsetStart = lineStart;
            }

            cur.tags[key] = val;
            res.bytesProcessed = lastAfter;
            continue;
        }

        // Movetext line.
        if (!inGame) {
            // Ignore preamble noise before first tag.
            res.bytesProcessed = lastAfter;
            continue;
        }

        appendMovetext(cur.movetext, trimmed);
        haveMovetext = true;
        res.bytesProcessed = lastAfter;
    }

    // EOF: flush last game if any.
    if (inGame) {
        cur.offsetEnd = fileSize;
        finalizeGame(cur);

        std::string cbErr;
        const bool cont = onGame(cur, res.bytesProcessed, &cbErr);
        if (!cbErr.empty()) {
            res.ok = false;
            res.error = cbErr;
            return res;
        }
        ++res.games;
        (void)cont;
    }

    res.ok = (res.games > 0);
    if (!res.ok) res.error = "No PGN games found";
    return res;
}

} // namespace sf::client::domain::pgn
