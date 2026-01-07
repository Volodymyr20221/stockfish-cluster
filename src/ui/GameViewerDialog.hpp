#pragma once

#include <QDialog>
#include <QString>

#include <cstdint>
#include <string>
#include <vector>

QT_BEGIN_NAMESPACE
class QListWidget;
class QLabel;
class QPushButton;
QT_END_NAMESPACE

namespace sf::client::domain::chess {
struct FenTimelineResult;
} // namespace sf::client::domain::chess

namespace sf::client::ui {

class BoardWidget;

class GameViewerDialog : public QDialog {
    Q_OBJECT

public:
    struct Meta {
        QString event;
        QString site;
        QString date;
        QString round;
        QString white;
        QString black;
        QString result;
        QString startFen; // optional full FEN
    };

    explicit GameViewerDialog(QWidget* parent = nullptr);

    // Loads a ready-made SAN timeline into the viewer.
    // Start position is meta.startFen if provided, otherwise timeline.startFen.
    bool setGame(const Meta& meta,
                 const sf::client::domain::chess::FenTimelineResult& timeline,
                 QString* outError = nullptr);

signals:
    // Requests analysis of the currently selected position.
    void analyzeRequested(const QString& fen, const QString& opponentHint);

private slots:
    void onMoveSelectionChanged();
    void onFirstClicked();
    void onPrevClicked();
    void onNextClicked();
    void onLastClicked();
    void onAnalyzeClicked();

private:
    struct Ply {
        int plyIndex{0};
        std::string san;
        std::string uci;
        std::string fenAfter;
        std::uint64_t posHashBefore{0};
    };

    void setupUi();
    void rebuildMovesList();
    void setCurrentPly(int plyIndex); // -1=start
    QString currentFen() const;
    QString opponentHint() const;

private:
    BoardWidget* boardWidget_{nullptr};
    QListWidget* movesList_{nullptr};
    QLabel* headerLabel_{nullptr};

    QPushButton* firstBtn_{nullptr};
    QPushButton* prevBtn_{nullptr};
    QPushButton* nextBtn_{nullptr};
    QPushButton* lastBtn_{nullptr};
    QPushButton* analyzeBtn_{nullptr};

    Meta meta_;
    QString startFen_;
    std::vector<Ply> plies_;

    int currentPly_{-1};
    bool ignoreSelection_{false};
};

} // namespace sf::client::ui
