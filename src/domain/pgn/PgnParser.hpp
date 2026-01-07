#pragma once

#include <map>
#include <string>
#include <vector>

namespace sf::client::domain::pgn {

struct PgnGame {
    std::map<std::string, std::string> tags; // e.g. "White", "Black", "Result", "FEN"
    std::string movetext;                   // raw movetext (may include comments/variations)
};

struct PgnParseResult {
    bool ok{false};
    std::string error;
    std::vector<PgnGame> games;
};

// Parses up to maxGames games from a PGN text.
// This is a pragmatic parser intended for UI workflows (viewer/import preview).
// It extracts tag pairs and concatenates movetext lines with spaces.
PgnParseResult parsePgnText(const std::string& text, int maxGames = 1);

} // namespace sf::client::domain::pgn
