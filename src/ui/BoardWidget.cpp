#include "ui/BoardWidget.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QFont>
#include <QtMath>
#include <cmath>
#include <algorithm>

namespace sf::client::ui {

static bool isWhitePiece(QChar c) { return c.isUpper(); }

static QChar fenToUnicodePiece(QChar fenChar) {
    // Unicode chess symbols: white U+2654..2659, black U+265A..265F
    switch (fenChar.toLatin1()) {
        case 'K': return QChar(0x2654);
        case 'Q': return QChar(0x2655);
        case 'R': return QChar(0x2656);
        case 'B': return QChar(0x2657);
        case 'N': return QChar(0x2658);
        case 'P': return QChar(0x2659);
        case 'k': return QChar(0x265A);
        case 'q': return QChar(0x265B);
        case 'r': return QChar(0x265C);
        case 'b': return QChar(0x265D);
        case 'n': return QChar(0x265E);
        case 'p': return QChar(0x265F);
        default:  return QChar('?');
    }
}

BoardWidget::BoardWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(320, 320);
    for (int r = 0; r < 8; ++r)
        for (int f = 0; f < 8; ++f)
            pieces_[r][f] = Piece{QChar(' ')};
}

void BoardWidget::setFen(const QString& fen) {
    fen_ = fen;
    parseFenPieces(fen_);
    update();
}

void BoardWidget::setArrows(const QVector<Arrow>& arrows) {
    arrows_ = arrows;
    update();
}

void BoardWidget::setHighlights(const QVector<Square>& squares) {
    highlights_ = squares;
    update();
}

std::optional<Square> BoardWidget::parseSquare(const QString& s) {
    if (s.size() != 2) return std::nullopt;
    const QChar fileC = s[0].toLower();
    const QChar rankC = s[1];
    if (fileC < 'a' || fileC > 'h') return std::nullopt;
    if (rankC < '1' || rankC > '8') return std::nullopt;

    Square sq;
    sq.file = fileC.unicode() - QChar('a').unicode();
    sq.rank = rankC.unicode() - QChar('1').unicode();
    return sq;
}

std::optional<Arrow> BoardWidget::arrowFromUciMove(const QString& uciMove,
                                                   std::optional<int> scoreCp,
                                                   std::optional<int> scoreMate,
                                                   int multipv) {
    if (uciMove.size() < 4) return std::nullopt;
    const auto from = parseSquare(uciMove.mid(0, 2));
    const auto to   = parseSquare(uciMove.mid(2, 2));
    if (!from || !to) return std::nullopt;

    Arrow a;
    a.from = *from;
    a.to = *to;
    a.scoreCp = scoreCp;
    a.scoreMate = scoreMate;
    a.multipv = multipv;
    return a;
}

void BoardWidget::parseFenPieces(const QString& fen) {
    // clear
    for (int r = 0; r < 8; ++r)
        for (int f = 0; f < 8; ++f)
            pieces_[r][f] = Piece{QChar(' ')};

    const QString placement = fen.split(' ').value(0);
    const QStringList ranks = placement.split('/');
    if (ranks.size() != 8) return;

    // FEN rank8 -> top. Our internal rank index 7=8th, 0=1st.
    for (int fenRank = 0; fenRank < 8; ++fenRank) {
        const QString row = ranks[fenRank];
        int file = 0;
        for (int i = 0; i < row.size() && file < 8; ++i) {
            const QChar c = row[i];
            if (c.isDigit()) {
                file += c.digitValue();
            } else {
                const int rankInternal = 7 - fenRank; // fenRank 0 => rank 7 (8th)
                pieces_[rankInternal][file] = Piece{c};
                file += 1;
            }
        }
    }
}

QRectF BoardWidget::boardRect() const {
    const qreal m = 12.0;
    const qreal s = qMin(width(), height()) - 2*m;
    return QRectF((width() - s)/2.0, (height() - s)/2.0, s, s);
}

QRectF BoardWidget::squareRect(int file, int rank) const {
    // rank: 0..7 (1..8), y=0 is top. White at bottom => rank 7 on top.
    const QRectF br = boardRect();
    const qreal sq = br.width() / 8.0;

    const int yIndex = 7 - rank;
    return QRectF(br.left() + file*sq, br.top() + yIndex*sq, sq, sq);
}

QPointF BoardWidget::squareCenter(int file, int rank) const {
    const QRectF r = squareRect(file, rank);
    return QPointF(r.center().x(), r.center().y());
}

void BoardWidget::paintEvent(QPaintEvent* ev) {
    Q_UNUSED(ev);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    drawBoard(p);
    drawHighlights(p);
    drawPieces(p);
    drawArrows(p);

    // border
    p.setPen(QPen(QColor(0,0,0,60), 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawRect(boardRect());
}

void BoardWidget::drawBoard(QPainter& p) {
    const QRectF br = boardRect();
    Q_UNUSED(br);

    const QColor light(240, 217, 181);
    const QColor dark (181, 136,  99);

    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            const QRectF r = squareRect(file, rank);
            const bool isLight = ((file + rank) % 2 == 0);
            p.fillRect(r, isLight ? light : dark);
        }
    }
}

void BoardWidget::drawHighlights(QPainter& p) {
    if (highlights_.isEmpty()) return;
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 0, 70));
    for (const auto& sq : highlights_) {
        p.drawRect(squareRect(sq.file, sq.rank));
    }
    p.restore();
}

void BoardWidget::drawPieces(QPainter& p) {
    p.save();

    const QRectF br = boardRect();
    const qreal sq = br.width()/8.0;

    QFont font("Segoe UI Symbol");
    font.setPixelSize(static_cast<int>(sq * 0.78));
    p.setFont(font);

    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            const QChar c = pieces_[rank][file].c;
            if (c == ' ') continue;

            const QRectF r = squareRect(file, rank);
            const QChar uni = fenToUnicodePiece(c);

            // simple piece tint
            p.setPen(isWhitePiece(c) ? QColor(250, 250, 250) : QColor(30, 30, 30));
            p.drawText(r, Qt::AlignCenter, QString(uni));
        }
    }

    p.restore();
}

static QColor arrowColorForScore(std::optional<int> cp, std::optional<int> mate) {
    // simple scheme:
    // mate => purple, cp>0 green, cp<0 red, cp==0 gray
    if (mate.has_value()) {
        return QColor(160, 90, 220, 180);
    }
    if (!cp.has_value()) {
        return QColor(80, 80, 80, 160);
    }
    if (*cp > 0) return QColor( 40, 180,  70, 180);
    if (*cp < 0) return QColor(220,  60,  60, 180);
    return QColor(80, 80, 80, 160);
}

static qreal arrowWidthForScore(std::optional<int> cp, std::optional<int> mate, qreal base) {
    if (mate.has_value()) return base * 1.6;
    if (!cp.has_value()) return base;
    const int v = qAbs(*cp);
    const qreal t = qMin<qreal>(1.0, v / 200.0); // 0..200cp => 0..1
    return base * (1.0 + 1.0 * t); // up to 2x
}

void BoardWidget::drawArrows(QPainter& p) {
    if (arrows_.isEmpty()) return;

    const QRectF br = boardRect();
    const qreal sq = br.width()/8.0;
    // Lichess-like feel: smaller arrow-head, dynamic sizing for short moves,
    // plus a subtle shadow so the arrow reads better on top of pieces.
    const qreal baseW = qMax<qreal>(2.0, sq * 0.06);
    const qreal headLBase = sq * 0.22;
    const qreal headWBase = sq * 0.16;

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    // Draw weaker lines first so the best line stays on top.
    QVector<Arrow> arrows = arrows_;
    std::sort(arrows.begin(), arrows.end(), [](const Arrow& a, const Arrow& b) {
        return a.multipv > b.multipv; // multipv=1 last
    });

    for (const auto& a : arrows) {
        const QPointF start = squareCenter(a.from.file, a.from.rank);
        const QPointF end   = squareCenter(a.to.file,   a.to.rank);

        const QPointF v = end - start;
        const qreal len = std::hypot(v.x(), v.y());
        if (len < 1e-3) continue;

        const QPointF dir(v.x()/len, v.y()/len);
        const QPointF ort(-dir.y(), dir.x());

        // Dynamic head sizes: on short moves the head must not dominate the arrow.
        const qreal headL = qMin(headLBase, len * 0.35);
        const qreal headW = qMin(headWBase, len * 0.28);

        // shorten so arrow head doesn't overshoot
        const QPointF end2 = end - dir * headL;

        const QColor col = arrowColorForScore(a.scoreCp, a.scoreMate);
        const qreal w = arrowWidthForScore(a.scoreCp, a.scoreMate, baseW);

        // Shadow pass (improves readability over pieces)
        {
            QPen shadowPen(QColor(0, 0, 0, 70), w + qMax<qreal>(2.0, w * 0.35), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            p.setPen(shadowPen);
            p.drawLine(start, end2);

            QPainterPath shadowHead;
            const QPointF tip = end;
            const QPointF left  = end2 + ort * (headW * 0.5);
            const QPointF right = end2 - ort * (headW * 0.5);
            shadowHead.moveTo(tip);
            shadowHead.lineTo(left);
            shadowHead.lineTo(right);
            shadowHead.closeSubpath();
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, 70));
            p.drawPath(shadowHead);
        }

        // Color pass
        QPen pen(col, w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        p.drawLine(start, end2);

        // head triangle
        QPainterPath head;
        const QPointF tip = end;
        const QPointF left  = end2 + ort * (headW * 0.5);
        const QPointF right = end2 - ort * (headW * 0.5);

        head.moveTo(tip);
        head.lineTo(left);
        head.lineTo(right);
        head.closeSubpath();

        p.setPen(Qt::NoPen);
        p.setBrush(col);
        p.drawPath(head);
    }

    p.restore();
}

} // namespace sf::client::ui
