#pragma once

#include <QWidget>
#include <QString>
#include <QRectF>
#include <QPointF>
#include <QVector>
#include <optional>

class QPainter;

namespace sf::client::ui {

struct Square {
    int file = 0; // 0=a..7=h
    int rank = 0; // 0=1..7=8
};

struct Arrow {
    Square from;
    Square to;
    // Evaluation relative to side-to-move (like Stockfish score)
    std::optional<int> scoreCp;     // e.g. +32
    std::optional<int> scoreMate;   // e.g. -3
    int multipv = 1;
};

class BoardWidget : public QWidget {
    Q_OBJECT
public:
    explicit BoardWidget(QWidget* parent = nullptr);

    void setFen(const QString& fen);
    void setArrows(const QVector<Arrow>& arrows);
    void setHighlights(const QVector<Square>& squares);

    static std::optional<Square> parseSquare(const QString& s); // "e2"
    static std::optional<Arrow> arrowFromUciMove(const QString& uciMove,
                                                 std::optional<int> scoreCp = std::nullopt,
                                                 std::optional<int> scoreMate = std::nullopt,
                                                 int multipv = 1);

protected:
    void paintEvent(QPaintEvent* ev) override;

private:
    struct Piece {
        QChar c; // 'P','n', etc
    };

    void parseFenPieces(const QString& fen);
    QRectF boardRect() const;
    QRectF squareRect(int file, int rank) const; // rank: 0..7 (1..8)
    QPointF squareCenter(int file, int rank) const;

    void drawBoard(QPainter& p);
    void drawHighlights(QPainter& p);
    void drawPieces(QPainter& p);
    void drawArrows(QPainter& p);

    // internal: [rank][file] with rank 0=1st rank
    Piece pieces_[8][8];

    QString fen_;
    QVector<Arrow> arrows_;
    QVector<Square> highlights_;
};

} // namespace sf::client::ui
