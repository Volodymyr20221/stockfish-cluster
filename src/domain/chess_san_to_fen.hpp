#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sf::client::domain::chess {

struct FenFromSanResult {
    bool ok{false};
    std::string fen;
    std::string error;
    int plyCount{0};
};

struct FenTimelinePly {
    int plyIndex{0};                // 0-based ply number
    std::string san;                // normalized SAN token (as parsed)
    std::string uci;                // UCI move (e2e4, e7e8q, etc.)
    std::string fenAfter;           // full FEN after the move (includes counters)
    std::uint64_t posHashBefore{0}; // hash of the position before the move (counters excluded)
};

struct FenTimelineResult {
    bool ok{false};
    std::string error;
    std::string startFen; // full FEN of the starting position (includes counters)
    std::vector<FenTimelinePly> plies;
};

// Converts a PGN/SAN move sequence like:
//   "1.d4 d5 2.c4 e6 3.Nf3 ..."
// into a FEN of the final position.
//
// Supported (pragmatic subset):
// - move numbers: "1.d4", "12...Nf6" (numbers are ignored)
// - captures: "Nxe5", pawn captures: "dxc4"
// - disambiguation: "Nbd7", "Rfd1", "Q1e2"
// - castling: "O-O", "O-O-O" (also accepts "0-0" / "0-0-0")
// - check/mate suffix: "+" / "#" (ignored)
// - promotions: "e8=Q", "fxg8=N+"
//
// By default starts from the standard initial position.
// If startFen is provided, moves are applied from that position instead.
FenFromSanResult fenFromSanMoves(const std::string& sanMoves,
                                 const std::optional<std::string>& startFen = std::nullopt);

// Builds a ply-by-ply timeline for a SAN/PGN movetext.
// Unlike fenFromSanMoves(), an empty move string is allowed: ok=true with plies empty.
FenTimelineResult fenTimelineFromSanMoves(const std::string& sanMoves,
                                         const std::optional<std::string>& startFen = std::nullopt);

} // namespace sf::client::domain::chess
