#pragma once

#include <optional>
#include <string>

namespace sf::client::domain::chess {

struct FenFromSanResult {
    bool ok{false};
    std::string fen;
    std::string error;
    int plyCount{0};
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

} // namespace sf::client::domain::chess
