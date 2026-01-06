#include "domain/chess_san_to_fen.hpp"

#include <array>
#include <cctype>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sf::client::domain::chess {

namespace {

// --------------------------- Basic board model -----------------------------

enum class Color { White, Black };

enum class Piece {
    Empty,
    WP, WN, WB, WR, WQ, WK,
    BP, BN, BB, BR, BQ, BK
};

inline Color opposite(Color c) {
    return (c == Color::White) ? Color::Black : Color::White;
}

inline bool isWhite(Piece p) {
    return p == Piece::WP || p == Piece::WN || p == Piece::WB ||
           p == Piece::WR || p == Piece::WQ || p == Piece::WK;
}

inline bool isBlack(Piece p) {
    return p == Piece::BP || p == Piece::BN || p == Piece::BB ||
           p == Piece::BR || p == Piece::BQ || p == Piece::BK;
}

inline bool isColor(Piece p, Color c) {
    return (c == Color::White) ? isWhite(p) : isBlack(p);
}

inline bool isEmpty(Piece p) {
    return p == Piece::Empty;
}

inline int fileOf(int sq) { return sq & 7; }
inline int rankOf(int sq) { return sq >> 3; }
inline bool onBoard(int f, int r) { return f >= 0 && f < 8 && r >= 0 && r < 8; }
inline int sqOf(int f, int r) { return (r << 3) | f; }

inline char fileChar(int f) { return static_cast<char>('a' + f); }
inline char rankChar(int r) { return static_cast<char>('1' + r); }

inline std::string sqToAlg(int sq) {
    std::string s;
    s.push_back(fileChar(fileOf(sq)));
    s.push_back(rankChar(rankOf(sq)));
    return s;
}

inline std::optional<int> algToSq(std::string_view sv) {
    if (sv.size() != 2) return std::nullopt;
    const char f = sv[0];
    const char r = sv[1];
    if (f < 'a' || f > 'h') return std::nullopt;
    if (r < '1' || r > '8') return std::nullopt;
    return sqOf(f - 'a', r - '1');
}

inline char pieceToFenChar(Piece p) {
    switch (p) {
        case Piece::WP: return 'P';
        case Piece::WN: return 'N';
        case Piece::WB: return 'B';
        case Piece::WR: return 'R';
        case Piece::WQ: return 'Q';
        case Piece::WK: return 'K';
        case Piece::BP: return 'p';
        case Piece::BN: return 'n';
        case Piece::BB: return 'b';
        case Piece::BR: return 'r';
        case Piece::BQ: return 'q';
        case Piece::BK: return 'k';
        default: return 0;
    }
}

inline std::optional<Piece> fenCharToPiece(char c) {
    switch (c) {
        case 'P': return Piece::WP;
        case 'N': return Piece::WN;
        case 'B': return Piece::WB;
        case 'R': return Piece::WR;
        case 'Q': return Piece::WQ;
        case 'K': return Piece::WK;
        case 'p': return Piece::BP;
        case 'n': return Piece::BN;
        case 'b': return Piece::BB;
        case 'r': return Piece::BR;
        case 'q': return Piece::BQ;
        case 'k': return Piece::BK;
        default: return std::nullopt;
    }
}

// ------------------------------ Move model --------------------------------

struct Move {
    int from{-1};
    int to{-1};
    Piece promotion{Piece::Empty};
    bool isCapture{false};
    bool isEnPassant{false};
    bool isCastleKing{false};
    bool isCastleQueen{false};
};

// ------------------------------ Position ----------------------------------

struct Position {
    std::array<Piece, 64> board{};
    Color stm{Color::White};
    bool wK{true}, wQ{true}, bK{true}, bQ{true};
    std::optional<int> epSq; // target square, 0..63
    int halfmove{0};
    int fullmove{1};

    static Position startpos() {
        Position p;
        p.board.fill(Piece::Empty);
        // Rank 1
        p.board[sqOf(0,0)] = Piece::WR;
        p.board[sqOf(1,0)] = Piece::WN;
        p.board[sqOf(2,0)] = Piece::WB;
        p.board[sqOf(3,0)] = Piece::WQ;
        p.board[sqOf(4,0)] = Piece::WK;
        p.board[sqOf(5,0)] = Piece::WB;
        p.board[sqOf(6,0)] = Piece::WN;
        p.board[sqOf(7,0)] = Piece::WR;
        // Rank 2
        for (int f = 0; f < 8; ++f) p.board[sqOf(f,1)] = Piece::WP;
        // Rank 7
        for (int f = 0; f < 8; ++f) p.board[sqOf(f,6)] = Piece::BP;
        // Rank 8
        p.board[sqOf(0,7)] = Piece::BR;
        p.board[sqOf(1,7)] = Piece::BN;
        p.board[sqOf(2,7)] = Piece::BB;
        p.board[sqOf(3,7)] = Piece::BQ;
        p.board[sqOf(4,7)] = Piece::BK;
        p.board[sqOf(5,7)] = Piece::BB;
        p.board[sqOf(6,7)] = Piece::BN;
        p.board[sqOf(7,7)] = Piece::BR;

        p.stm = Color::White;
        p.wK = p.wQ = p.bK = p.bQ = true;
        p.epSq = std::nullopt;
        p.halfmove = 0;
        p.fullmove = 1;
        return p;
    }

    static std::optional<Position> fromFen(const std::string& fen) {
        std::istringstream in(fen);
        std::string placement, active, castling, ep;
        int half = 0, full = 1;
        if (!(in >> placement >> active >> castling >> ep >> half >> full)) {
            return std::nullopt;
        }

        Position p;
        p.board.fill(Piece::Empty);

        int r = 7;
        int f = 0;
        for (char c : placement) {
            if (c == '/') {
                --r;
                f = 0;
                continue;
            }
            if (std::isdigit(static_cast<unsigned char>(c))) {
                f += (c - '0');
                continue;
            }
            auto pc = fenCharToPiece(c);
            if (!pc) return std::nullopt;
            if (!onBoard(f, r)) return std::nullopt;
            p.board[sqOf(f, r)] = *pc;
            ++f;
        }
        if (r != 0) {
            // placement didn't consume all ranks
            // (we accept, but still validate basic shape)
        }

        if (active == "w") p.stm = Color::White;
        else if (active == "b") p.stm = Color::Black;
        else return std::nullopt;

        p.wK = p.wQ = p.bK = p.bQ = false;
        if (castling != "-") {
            for (char c : castling) {
                if (c == 'K') p.wK = true;
                else if (c == 'Q') p.wQ = true;
                else if (c == 'k') p.bK = true;
                else if (c == 'q') p.bQ = true;
                else return std::nullopt;
            }
        }

        if (ep == "-") {
            p.epSq = std::nullopt;
        } else {
            auto sq = algToSq(ep);
            if (!sq) return std::nullopt;
            p.epSq = *sq;
        }

        p.halfmove = half;
        p.fullmove = full;
        return p;
    }

    std::string toFen() const {
        std::string placement;
        placement.reserve(80);
        for (int r = 7; r >= 0; --r) {
            int emptyRun = 0;
            for (int f = 0; f < 8; ++f) {
                const Piece p = board[sqOf(f, r)];
                if (p == Piece::Empty) {
                    ++emptyRun;
                } else {
                    if (emptyRun > 0) {
                        placement.push_back(static_cast<char>('0' + emptyRun));
                        emptyRun = 0;
                    }
                    placement.push_back(pieceToFenChar(p));
                }
            }
            if (emptyRun > 0) {
                placement.push_back(static_cast<char>('0' + emptyRun));
            }
            if (r != 0) placement.push_back('/');
        }

        std::string cast;
        if (wK) cast.push_back('K');
        if (wQ) cast.push_back('Q');
        if (bK) cast.push_back('k');
        if (bQ) cast.push_back('q');
        if (cast.empty()) cast = "-";

        const std::string ep = epSq ? sqToAlg(*epSq) : "-";

        std::ostringstream out;
        out << placement << ' ';
        out << ((stm == Color::White) ? 'w' : 'b') << ' ';
        out << cast << ' ';
        out << ep << ' ';
        out << halfmove << ' ';
        out << fullmove;
        return out.str();
    }

    std::optional<int> kingSquare(Color c) const {
        const Piece k = (c == Color::White) ? Piece::WK : Piece::BK;
        for (int i = 0; i < 64; ++i) {
            if (board[i] == k) return i;
        }
        return std::nullopt;
    }

    bool squareAttackedBy(int sq, Color by) const {
        const int f = fileOf(sq);
        const int r = rankOf(sq);

        // Pawns
        if (by == Color::White) {
            const int rr = r - 1;
            if (rr >= 0) {
                if (f - 1 >= 0 && board[sqOf(f - 1, rr)] == Piece::WP) return true;
                if (f + 1 < 8 && board[sqOf(f + 1, rr)] == Piece::WP) return true;
            }
        } else {
            const int rr = r + 1;
            if (rr < 8) {
                if (f - 1 >= 0 && board[sqOf(f - 1, rr)] == Piece::BP) return true;
                if (f + 1 < 8 && board[sqOf(f + 1, rr)] == Piece::BP) return true;
            }
        }

        // Knights
        static const int kD[8][2] = {
            {1,2},{2,1},{2,-1},{1,-2},{-1,-2},{-2,-1},{-2,1},{-1,2}
        };
        const Piece n = (by == Color::White) ? Piece::WN : Piece::BN;
        for (auto& d : kD) {
            const int nf = f + d[0];
            const int nr = r + d[1];
            if (onBoard(nf, nr) && board[sqOf(nf, nr)] == n) return true;
        }

        // King
        const Piece k = (by == Color::White) ? Piece::WK : Piece::BK;
        for (int df = -1; df <= 1; ++df) {
            for (int dr = -1; dr <= 1; ++dr) {
                if (df == 0 && dr == 0) continue;
                const int nf = f + df;
                const int nr = r + dr;
                if (onBoard(nf, nr) && board[sqOf(nf, nr)] == k) return true;
            }
        }

        // Sliding pieces
        const Piece b = (by == Color::White) ? Piece::WB : Piece::BB;
        const Piece rP = (by == Color::White) ? Piece::WR : Piece::BR;
        const Piece q = (by == Color::White) ? Piece::WQ : Piece::BQ;

        // Diagonals
        static const int diagD[4][2] = {{1,1},{1,-1},{-1,1},{-1,-1}};
        for (auto& d : diagD) {
            int nf = f + d[0];
            int nr = r + d[1];
            while (onBoard(nf, nr)) {
                const Piece p = board[sqOf(nf, nr)];
                if (!isEmpty(p)) {
                    if (p == b || p == q) return true;
                    break;
                }
                nf += d[0];
                nr += d[1];
            }
        }

        // Orthogonals
        static const int ortD[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
        for (auto& d : ortD) {
            int nf = f + d[0];
            int nr = r + d[1];
            while (onBoard(nf, nr)) {
                const Piece p = board[sqOf(nf, nr)];
                if (!isEmpty(p)) {
                    if (p == rP || p == q) return true;
                    break;
                }
                nf += d[0];
                nr += d[1];
            }
        }

        return false;
    }

    bool inCheck(Color c) const {
        auto ks = kingSquare(c);
        if (!ks) return true; // invalid position treated as "in check"
        return squareAttackedBy(*ks, opposite(c));
    }

    void addMove(std::vector<Move>& out, int from, int to, bool capture = false) const {
        Move m;
        m.from = from;
        m.to = to;
        m.isCapture = capture;
        out.push_back(m);
    }

    std::vector<Move> pseudoMoves() const {
        std::vector<Move> moves;
        moves.reserve(64);

        const Color us = stm;

        for (int sq = 0; sq < 64; ++sq) {
            const Piece p = board[sq];
            if (isEmpty(p) || !isColor(p, us)) continue;

            const int f = fileOf(sq);
            const int r = rankOf(sq);

            // Pawns
            if (p == Piece::WP || p == Piece::BP) {
                const int dir = (p == Piece::WP) ? +1 : -1;
                const int startRank = (p == Piece::WP) ? 1 : 6;
                const int promoRank = (p == Piece::WP) ? 7 : 0;

                // forward 1
                const int r1 = r + dir;
                if (r1 >= 0 && r1 < 8) {
                    const int to = sqOf(f, r1);
                    if (isEmpty(board[to])) {
                        if (r1 == promoRank) {
                            for (Piece pr : { (us==Color::White)?Piece::WQ:Piece::BQ,
                                              (us==Color::White)?Piece::WR:Piece::BR,
                                              (us==Color::White)?Piece::WB:Piece::BB,
                                              (us==Color::White)?Piece::WN:Piece::BN }) {
                                Move m;
                                m.from = sq;
                                m.to = to;
                                m.promotion = pr;
                                m.isCapture = false;
                                moves.push_back(m);
                            }
                        } else {
                            addMove(moves, sq, to, false);
                        }

                        // forward 2
                        if (r == startRank) {
                            const int r2 = r + 2*dir;
                            const int to2 = sqOf(f, r2);
                            if (isEmpty(board[to2])) {
                                addMove(moves, sq, to2, false);
                            }
                        }
                    }
                }

                // captures
                for (int df : {-1, +1}) {
                    const int nf = f + df;
                    const int nr = r + dir;
                    if (!onBoard(nf, nr)) continue;
                    const int to = sqOf(nf, nr);
                    const Piece tp = board[to];
                    const bool normalCap = !isEmpty(tp) && isColor(tp, opposite(us));
                    const bool epCap = epSq && (*epSq == to);

                    if (normalCap || epCap) {
                        if (nr == promoRank) {
                            for (Piece pr : { (us==Color::White)?Piece::WQ:Piece::BQ,
                                              (us==Color::White)?Piece::WR:Piece::BR,
                                              (us==Color::White)?Piece::WB:Piece::BB,
                                              (us==Color::White)?Piece::WN:Piece::BN }) {
                                Move m;
                                m.from = sq;
                                m.to = to;
                                m.promotion = pr;
                                m.isCapture = true;
                                m.isEnPassant = epCap;
                                moves.push_back(m);
                            }
                        } else {
                            Move m;
                            m.from = sq;
                            m.to = to;
                            m.isCapture = true;
                            m.isEnPassant = epCap;
                            moves.push_back(m);
                        }
                    }
                }
                continue;
            }

            // Knights
            if (p == Piece::WN || p == Piece::BN) {
                static const int kD[8][2] = {
                    {1,2},{2,1},{2,-1},{1,-2},{-1,-2},{-2,-1},{-2,1},{-1,2}
                };
                for (auto& d : kD) {
                    const int nf = f + d[0];
                    const int nr = r + d[1];
                    if (!onBoard(nf, nr)) continue;
                    const int to = sqOf(nf, nr);
                    const Piece tp = board[to];
                    if (isEmpty(tp)) addMove(moves, sq, to, false);
                    else if (isColor(tp, opposite(us))) addMove(moves, sq, to, true);
                }
                continue;
            }

            // Kings (with castling)
            if (p == Piece::WK || p == Piece::BK) {
                for (int df = -1; df <= 1; ++df) {
                    for (int dr = -1; dr <= 1; ++dr) {
                        if (df == 0 && dr == 0) continue;
                        const int nf = f + df;
                        const int nr = r + dr;
                        if (!onBoard(nf, nr)) continue;
                        const int to = sqOf(nf, nr);
                        const Piece tp = board[to];
                        if (isEmpty(tp)) addMove(moves, sq, to, false);
                        else if (isColor(tp, opposite(us))) addMove(moves, sq, to, true);
                    }
                }

                // Castling
                if (us == Color::White && sq == sqOf(4,0) && p == Piece::WK) {
                    if (wK && board[sqOf(5,0)] == Piece::Empty && board[sqOf(6,0)] == Piece::Empty && board[sqOf(7,0)] == Piece::WR) {
                        Move m;
                        m.from = sqOf(4,0);
                        m.to = sqOf(6,0);
                        m.isCastleKing = true;
                        moves.push_back(m);
                    }
                    if (wQ && board[sqOf(3,0)] == Piece::Empty && board[sqOf(2,0)] == Piece::Empty && board[sqOf(1,0)] == Piece::Empty && board[sqOf(0,0)] == Piece::WR) {
                        Move m;
                        m.from = sqOf(4,0);
                        m.to = sqOf(2,0);
                        m.isCastleQueen = true;
                        moves.push_back(m);
                    }
                }
                if (us == Color::Black && sq == sqOf(4,7) && p == Piece::BK) {
                    if (bK && board[sqOf(5,7)] == Piece::Empty && board[sqOf(6,7)] == Piece::Empty && board[sqOf(7,7)] == Piece::BR) {
                        Move m;
                        m.from = sqOf(4,7);
                        m.to = sqOf(6,7);
                        m.isCastleKing = true;
                        moves.push_back(m);
                    }
                    if (bQ && board[sqOf(3,7)] == Piece::Empty && board[sqOf(2,7)] == Piece::Empty && board[sqOf(1,7)] == Piece::Empty && board[sqOf(0,7)] == Piece::BR) {
                        Move m;
                        m.from = sqOf(4,7);
                        m.to = sqOf(2,7);
                        m.isCastleQueen = true;
                        moves.push_back(m);
                    }
                }
                continue;
            }

            // Sliding pieces
            auto slide = [&](const std::vector<std::pair<int,int>>& dirs) {
                for (auto [df, dr] : dirs) {
                    int nf = f + df;
                    int nr = r + dr;
                    while (onBoard(nf, nr)) {
                        const int to = sqOf(nf, nr);
                        const Piece tp = board[to];
                        if (isEmpty(tp)) {
                            addMove(moves, sq, to, false);
                        } else {
                            if (isColor(tp, opposite(us))) addMove(moves, sq, to, true);
                            break;
                        }
                        nf += df;
                        nr += dr;
                    }
                }
            };

            if (p == Piece::WB || p == Piece::BB) {
                slide({{1,1},{1,-1},{-1,1},{-1,-1}});
                continue;
            }
            if (p == Piece::WR || p == Piece::BR) {
                slide({{1,0},{-1,0},{0,1},{0,-1}});
                continue;
            }
            if (p == Piece::WQ || p == Piece::BQ) {
                slide({{1,1},{1,-1},{-1,1},{-1,-1},{1,0},{-1,0},{0,1},{0,-1}});
                continue;
            }
        }
        return moves;
    }

    std::vector<Move> legalMoves() const {
        std::vector<Move> out;
        out.reserve(64);
        const auto pseudo = pseudoMoves();
        for (const auto& m : pseudo) {
            Position copy = *this;
            if (!copy.applyMove(m)) continue;
            if (!copy.inCheck(opposite(copy.stm))) { // after apply, stm flipped; opposite is side who just moved
                // castling additional legality: king cannot pass through check
                if (m.isCastleKing || m.isCastleQueen) {
                    const Color mover = opposite(copy.stm);
                    if (!castlePathLegal(mover, m.isCastleKing)) continue;
                }
                out.push_back(m);
            }
        }
        return out;
    }

    bool castlePathLegal(Color mover, bool kingSide) const {
        // Check if king is not in check and does not pass through attacked squares.
        // This function expects the move to be present in pseudoMoves (pieces/empties/rook ok).
        // It checks attacks in the *current* position (before move), so should be called on original pos.
        const int homeRank = (mover == Color::White) ? 0 : 7;
        const int kingFrom = sqOf(4, homeRank);
        if (inCheck(mover)) return false;
        if (kingSide) {
            // e1->f1->g1 or e8->f8->g8
            if (squareAttackedBy(sqOf(5, homeRank), opposite(mover))) return false;
            if (squareAttackedBy(sqOf(6, homeRank), opposite(mover))) return false;
        } else {
            // e1->d1->c1 or e8->d8->c8
            if (squareAttackedBy(sqOf(3, homeRank), opposite(mover))) return false;
            if (squareAttackedBy(sqOf(2, homeRank), opposite(mover))) return false;
        }
        return true;
    }

    bool applyMove(const Move& m) {
        const Piece moving = board[m.from];
        if (isEmpty(moving)) return false;
        if (!isColor(moving, stm)) return false;

        // Remember whether capture or pawn move for halfmove clock.
        const bool pawnMove = (moving == Piece::WP || moving == Piece::BP);
        bool didCapture = m.isCapture;

        // Clear en-passant by default.
        epSq = std::nullopt;

        // Castling
        if (m.isCastleKing || m.isCastleQueen) {
            const int homeRank = (stm == Color::White) ? 0 : 7;
            const int kingFrom = sqOf(4, homeRank);
            if (m.from != kingFrom) return false;
            const int rookFrom = m.isCastleKing ? sqOf(7, homeRank) : sqOf(0, homeRank);
            const int kingTo   = m.isCastleKing ? sqOf(6, homeRank) : sqOf(2, homeRank);
            const int rookTo   = m.isCastleKing ? sqOf(5, homeRank) : sqOf(3, homeRank);

            const Piece rook = board[rookFrom];
            if (stm == Color::White) {
                if (rook != Piece::WR) return false;
            } else {
                if (rook != Piece::BR) return false;
            }

            board[kingFrom] = Piece::Empty;
            board[rookFrom] = Piece::Empty;
            board[kingTo] = moving;
            board[rookTo] = rook;

            // lose castling rights
            if (stm == Color::White) { wK = wQ = false; }
            else { bK = bQ = false; }

            // Castling is neither pawn move nor capture.
            halfmove += 1;
            if (stm == Color::Black) fullmove += 1;
            stm = opposite(stm);
            return true;
        }

        // En-passant capture
        if (m.isEnPassant) {
            const int toF = fileOf(m.to);
            const int toR = rankOf(m.to);
            const int capR = (stm == Color::White) ? (toR - 1) : (toR + 1);
            const int capSq = sqOf(toF, capR);
            board[capSq] = Piece::Empty;
            didCapture = true;
        }

        // Update castling rights when king or rook moves or rook is captured.
        auto clearCastlingIfRookSquare = [&](int sqCaptured) {
            if (sqCaptured == sqOf(0,0)) wQ = false;
            if (sqCaptured == sqOf(7,0)) wK = false;
            if (sqCaptured == sqOf(0,7)) bQ = false;
            if (sqCaptured == sqOf(7,7)) bK = false;
        };

        if (m.isCapture && !m.isEnPassant) {
            clearCastlingIfRookSquare(m.to);
        }

        if (moving == Piece::WK) { wK = wQ = false; }
        if (moving == Piece::BK) { bK = bQ = false; }

        if (moving == Piece::WR) {
            if (m.from == sqOf(0,0)) wQ = false;
            if (m.from == sqOf(7,0)) wK = false;
        }
        if (moving == Piece::BR) {
            if (m.from == sqOf(0,7)) bQ = false;
            if (m.from == sqOf(7,7)) bK = false;
        }

        // Perform capture / move.
        board[m.from] = Piece::Empty;
        board[m.to] = moving;

        // Promotion
        if (m.promotion != Piece::Empty) {
            board[m.to] = m.promotion;
        }

        // Set en-passant target if double pawn push and capture is possible.
        if (pawnMove) {
            const int fromR = rankOf(m.from);
            const int toR = rankOf(m.to);
            if (stm == Color::White && fromR == 1 && toR == 3) {
                const int ep = sqOf(fileOf(m.from), 2);
                if (canCaptureEp(Color::Black, ep)) epSq = ep;
            }
            if (stm == Color::Black && fromR == 6 && toR == 4) {
                const int ep = sqOf(fileOf(m.from), 5);
                if (canCaptureEp(Color::White, ep)) epSq = ep;
            }
        }

        // Halfmove clock
        if (pawnMove || didCapture) halfmove = 0;
        else halfmove += 1;

        // Fullmove
        if (stm == Color::Black) fullmove += 1;

        // Flip side
        stm = opposite(stm);
        return true;
    }

    bool canCaptureEp(Color capturer, int epTarget) const {
        const int f = fileOf(epTarget);
        const int r = rankOf(epTarget);
        const Piece pawn = (capturer == Color::White) ? Piece::WP : Piece::BP;
        const int pawnRank = (capturer == Color::White) ? (r - 1) : (r + 1);
        if (pawnRank < 0 || pawnRank >= 8) return false;
        for (int df : {-1, +1}) {
            const int nf = f + df;
            if (nf < 0 || nf >= 8) continue;
            if (board[sqOf(nf, pawnRank)] == pawn) return true;
        }
        return false;
    }
};

// ------------------------------- SAN parser --------------------------------

enum class PieceKind { Pawn, Knight, Bishop, Rook, Queen, King, CastleK, CastleQ };

struct MoveSpec {
    PieceKind kind{PieceKind::Pawn};
    int to{-1};
    bool capture{false};
    std::optional<int> disFile; // 0..7
    std::optional<int> disRank; // 0..7
    std::optional<char> promo;  // 'Q','R','B','N'
};

inline bool isMoveResultToken(const std::string& t) {
    return t == "1-0" || t == "0-1" || t == "1/2-1/2" || t == "*";
}

std::string stripPgnDecorations(std::string s) {
    // Trim trailing check/mate and annotation marks.
    while (!s.empty()) {
        char c = s.back();
        if (c == '+' || c == '#' || c == '!' || c == '?') {
            s.pop_back();
        } else {
            break;
        }
    }
    return s;
}

std::string stripLeadingMoveNumber(std::string s) {
    // Handles: "1.d4", "12...Nf6", "34." etc.
    // If there's a dot after a number prefix, keep the suffix.
    size_t i = 0;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
    if (i == 0) return s;
    if (i < s.size() && s[i] == '.') {
        // consume one or more dots
        while (i < s.size() && s[i] == '.') ++i;
        if (i < s.size()) return s.substr(i);
        return std::string{};
    }
    return s;
}

std::string removePgnCommentsAndVars(const std::string& in) {
    // Removes {...} comments, ; line comments, and (...) variations (supports nesting).
    std::string out;
    out.reserve(in.size());
    int brace = 0;
    int paren = 0;
    bool inSemiComment = false;
    for (size_t i = 0; i < in.size(); ++i) {
        const char c = in[i];
        if (inSemiComment) {
            if (c == '\n' || c == '\r') inSemiComment = false;
            continue;
        }
        if (brace > 0) {
            if (c == '{') ++brace;
            else if (c == '}') --brace;
            continue;
        }
        if (paren > 0) {
            if (c == '(') ++paren;
            else if (c == ')') --paren;
            continue;
        }
        if (c == ';') {
            inSemiComment = true;
            continue;
        }
        if (c == '{') { brace = 1; continue; }
        if (c == '(') { paren = 1; continue; }
        out.push_back(c);
    }
    return out;
}

std::vector<std::string> tokenizeMoves(const std::string& text) {
    std::vector<std::string> tokens;

    const std::string cleaned = removePgnCommentsAndVars(text);

    std::string cur;
    cur.reserve(16);
    for (char c : cleaned) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!cur.empty()) {
                tokens.push_back(cur);
                cur.clear();
            }
            continue;
        }
        cur.push_back(c);
    }
    if (!cur.empty()) tokens.push_back(cur);

    // Normalize: strip move numbers, decorations, and drop results.
    std::vector<std::string> out;
    out.reserve(tokens.size());
    for (auto t : tokens) {
        t = stripLeadingMoveNumber(t);
        t = stripPgnDecorations(t);
        if (t.empty()) continue;
        if (isMoveResultToken(t)) continue;
        out.push_back(t);
    }
    return out;
}

std::optional<MoveSpec> parseSanToken(const std::string& token) {
    if (token.empty()) return std::nullopt;

    auto ieq = [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    };

    // Castling
    if (token == "O-O" || token == "0-0" || token == "o-o") {
        MoveSpec s;
        s.kind = PieceKind::CastleK;
        return s;
    }
    if (token == "O-O-O" || token == "0-0-0" || token == "o-o-o") {
        MoveSpec s;
        s.kind = PieceKind::CastleQ;
        return s;
    }

    MoveSpec spec;
    size_t i = 0;

    // Piece letter or pawn
    const char c0 = token[0];
    if (c0 == 'N' || c0 == 'B' || c0 == 'R' || c0 == 'Q' || c0 == 'K') {
        if (c0 == 'N') spec.kind = PieceKind::Knight;
        else if (c0 == 'B') spec.kind = PieceKind::Bishop;
        else if (c0 == 'R') spec.kind = PieceKind::Rook;
        else if (c0 == 'Q') spec.kind = PieceKind::Queen;
        else if (c0 == 'K') spec.kind = PieceKind::King;
        i = 1;
    } else {
        spec.kind = PieceKind::Pawn;
        i = 0;
    }

    // Find promotion ("=Q")
    size_t promoPos = token.find('=');
    std::string_view core = token;
    if (promoPos != std::string::npos) {
        if (promoPos + 1 >= token.size()) return std::nullopt;
        const char pc = token[promoPos + 1];
        if (!(pc == 'Q' || pc == 'R' || pc == 'B' || pc == 'N')) return std::nullopt;
        spec.promo = pc;
        core = std::string_view(token.data(), promoPos);
    }

    // Destination square = last 2 chars of core
    if (core.size() < 2) return std::nullopt;
    const std::string_view dst = core.substr(core.size() - 2);
    auto toSq = algToSq(dst);
    if (!toSq) return std::nullopt;
    spec.to = *toSq;

    // Everything before destination are modifiers.
    std::string_view mods = core.substr(i, core.size() - 2 - i);

    // Capture marker
    size_t xPos = mods.find('x');
    if (xPos != std::string::npos) {
        spec.capture = true;
        // Remove 'x'
        std::string tmp;
        tmp.reserve(mods.size());
        for (char ch : mods) if (ch != 'x') tmp.push_back(ch);
        mods = tmp;
    }

    // Pawn moves: "e4" or "dxc4" => origin file can be present.
    if (spec.kind == PieceKind::Pawn) {
        if (!mods.empty()) {
            // Typically one char: origin file letter for captures ("dxc4")
            if (mods.size() == 1 && mods[0] >= 'a' && mods[0] <= 'h') {
                spec.disFile = mods[0] - 'a';
            } else {
                // Sometimes a pawn move can appear as "exd"? not supported.
                // If we can't interpret, reject.
                return std::nullopt;
            }
        }
        return spec;
    }

    // Piece moves: disambiguation may be 0..2 chars (file/rank)
    if (!mods.empty()) {
        if (mods.size() == 1) {
            const char d = mods[0];
            if (d >= 'a' && d <= 'h') spec.disFile = d - 'a';
            else if (d >= '1' && d <= '8') spec.disRank = d - '1';
            else return std::nullopt;
        } else if (mods.size() == 2) {
            const char d0 = mods[0];
            const char d1 = mods[1];
            if (d0 >= 'a' && d0 <= 'h') spec.disFile = d0 - 'a';
            else return std::nullopt;
            if (d1 >= '1' && d1 <= '8') spec.disRank = d1 - '1';
            else return std::nullopt;
        } else {
            return std::nullopt;
        }
    }

    return spec;
}

Piece pieceForKind(Color c, PieceKind k) {
    if (c == Color::White) {
        switch (k) {
            case PieceKind::Pawn: return Piece::WP;
            case PieceKind::Knight: return Piece::WN;
            case PieceKind::Bishop: return Piece::WB;
            case PieceKind::Rook: return Piece::WR;
            case PieceKind::Queen: return Piece::WQ;
            case PieceKind::King: return Piece::WK;
            default: return Piece::Empty;
        }
    } else {
        switch (k) {
            case PieceKind::Pawn: return Piece::BP;
            case PieceKind::Knight: return Piece::BN;
            case PieceKind::Bishop: return Piece::BB;
            case PieceKind::Rook: return Piece::BR;
            case PieceKind::Queen: return Piece::BQ;
            case PieceKind::King: return Piece::BK;
            default: return Piece::Empty;
        }
    }
}

std::optional<Move> pickMoveBySpec(const Position& pos, const MoveSpec& spec, std::string& err) {
    const auto legal = pos.legalMoves();

    std::vector<Move> matches;
    matches.reserve(4);

    if (spec.kind == PieceKind::CastleK || spec.kind == PieceKind::CastleQ) {
        const bool wantK = (spec.kind == PieceKind::CastleK);
        for (const auto& m : legal) {
            if (wantK && m.isCastleKing) matches.push_back(m);
            if (!wantK && m.isCastleQueen) matches.push_back(m);
        }
    } else {
        const Piece want = pieceForKind(pos.stm, spec.kind);
        for (const auto& m : legal) {
            if (m.isCastleKing || m.isCastleQueen) continue;
            if (m.to != spec.to) continue;
            if (pos.board[m.from] != want) continue;

            const bool cap = m.isCapture || m.isEnPassant;
            if (spec.capture != cap) continue;

            if (spec.promo) {
                if (m.promotion == Piece::Empty) continue;
                const char fenC = pieceToFenChar(m.promotion);
                if (!fenC) continue;
                if (std::toupper(static_cast<unsigned char>(fenC)) != *spec.promo) continue;
            } else {
                if (m.promotion != Piece::Empty) continue;
            }

            if (spec.disFile && fileOf(m.from) != *spec.disFile) continue;
            if (spec.disRank && rankOf(m.from) != *spec.disRank) continue;

            matches.push_back(m);
        }
    }

    if (matches.empty()) {
        err = "No legal move matches SAN token";
        return std::nullopt;
    }
    if (matches.size() > 1) {
        err = "Ambiguous SAN token (multiple legal moves match)";
        return std::nullopt;
    }
    return matches.front();
}

} // namespace

FenFromSanResult fenFromSanMoves(const std::string& sanMoves,
                                 const std::optional<std::string>& startFen) {
    FenFromSanResult res;

    Position pos;
    if (startFen) {
        auto p = Position::fromFen(*startFen);
        if (!p) {
            res.ok = false;
            res.error = "Invalid start FEN";
            return res;
        }
        pos = *p;
    } else {
        pos = Position::startpos();
    }

    const auto tokens = tokenizeMoves(sanMoves);
    if (tokens.empty()) {
        res.ok = false;
        res.error = "No moves found";
        return res;
    }

    int ply = 0;
    for (const auto& tRaw : tokens) {
        const std::string t = stripPgnDecorations(stripLeadingMoveNumber(tRaw));
        if (t.empty()) continue;
        if (isMoveResultToken(t)) break;

        const auto specOpt = parseSanToken(t);
        if (!specOpt) {
            res.ok = false;
            res.error = "Cannot parse SAN token: '" + t + "'";
            return res;
        }

        const MoveSpec spec = *specOpt;

        // Castling needs extra legality (passing through check) checked on pre-move position.
        if (spec.kind == PieceKind::CastleK) {
            if (!pos.castlePathLegal(pos.stm, true)) {
                res.ok = false;
                res.error = "Illegal castle (through check) at token: '" + t + "'";
                return res;
            }
        }
        if (spec.kind == PieceKind::CastleQ) {
            if (!pos.castlePathLegal(pos.stm, false)) {
                res.ok = false;
                res.error = "Illegal castle (through check) at token: '" + t + "'";
                return res;
            }
        }

        std::string err;
        auto mv = pickMoveBySpec(pos, spec, err);
        if (!mv) {
            res.ok = false;
            res.error = err + ": '" + t + "'";
            return res;
        }
        if (!pos.applyMove(*mv)) {
            res.ok = false;
            res.error = "Failed to apply move: '" + t + "'";
            return res;
        }
        ++ply;
    }

    res.ok = true;
    res.fen = pos.toFen();
    res.plyCount = ply;
    return res;
}

} // namespace sf::client::domain::chess
