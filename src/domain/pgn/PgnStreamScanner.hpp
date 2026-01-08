#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>

namespace sf::client::domain::pgn {

struct PgnStreamGame {
    std::uint64_t offsetStart{0}; // byte offset where the game's first tag line starts
    std::uint64_t offsetEnd{0};   // byte offset (exclusive) where the game ends

    std::map<std::string, std::string> tags; // "White", "Black", "Result", "FEN", ...
    std::string movetext;                   // concatenated movetext lines (raw, may include comments/variations)
};

struct PgnStreamScanResult {
    bool ok{false};
    std::string error;

    int games{0};
    std::uint64_t bytesProcessed{0};
};

// Streaming PGN file scanner.
//
// It reads the file line-by-line, extracts tag pairs and concatenates movetext lines with spaces.
// For each fully collected game it calls onGame(game, bytesProcessed, errorOut).
//
// onGame should return true to continue scanning, or false to stop early (treated as ok=true).
PgnStreamScanResult scanPgnFile(const std::string& filePath,
                                const std::function<bool(const PgnStreamGame&,
                                                         std::uint64_t bytesProcessed,
                                                         std::string* errorOut)>& onGame,
                                int maxGames = -1);

} // namespace sf::client::domain::pgn
