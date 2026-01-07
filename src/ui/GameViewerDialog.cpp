#include "ui/GameViewerDialog.hpp"

#include "domain/chess_san_to_fen.hpp"
#include "ui/BoardWidget.hpp"

#include <QAbstractItemView>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

namespace sf::client::ui {

using sf::client::domain::chess::FenTimelineResult;

GameViewerDialog::GameViewerDialog(QWidget* parent)
    : QDialog(parent) {
    setupUi();
}

void GameViewerDialog::setupUi() {
    resize(900, 600);
    setWindowTitle(tr("Game viewer"));

    auto* mainLayout = new QVBoxLayout(this);

    headerLabel_ = new QLabel(this);
    headerLabel_->setWordWrap(true);
    mainLayout->addWidget(headerLabel_);

    auto* splitter = new QSplitter(this);
    splitter->setOrientation(Qt::Horizontal);

    boardWidget_ = new BoardWidget(splitter);
    boardWidget_->setMinimumSize(360, 360);

    movesList_ = new QListWidget(splitter);
    movesList_->setSelectionMode(QAbstractItemView::SingleSelection);
    movesList_->setUniformItemSizes(true);

    splitter->addWidget(boardWidget_);
    splitter->addWidget(movesList_);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);

    mainLayout->addWidget(splitter, 1);

    auto* navLayout = new QHBoxLayout();

    firstBtn_ = new QPushButton(tr("<<"), this);
    prevBtn_  = new QPushButton(tr("<"), this);
    nextBtn_  = new QPushButton(tr(">"), this);
    lastBtn_  = new QPushButton(tr(">>"), this);
    analyzeBtn_ = new QPushButton(tr("Analyze this position"), this);

    navLayout->addWidget(firstBtn_);
    navLayout->addWidget(prevBtn_);
    navLayout->addWidget(nextBtn_);
    navLayout->addWidget(lastBtn_);
    navLayout->addStretch(1);
    navLayout->addWidget(analyzeBtn_);

    mainLayout->addLayout(navLayout);

    connect(movesList_, &QListWidget::currentRowChanged,
            this, &GameViewerDialog::onMoveSelectionChanged);

    connect(firstBtn_, &QPushButton::clicked, this, &GameViewerDialog::onFirstClicked);
    connect(prevBtn_,  &QPushButton::clicked, this, &GameViewerDialog::onPrevClicked);
    connect(nextBtn_,  &QPushButton::clicked, this, &GameViewerDialog::onNextClicked);
    connect(lastBtn_,  &QPushButton::clicked, this, &GameViewerDialog::onLastClicked);
    connect(analyzeBtn_, &QPushButton::clicked, this, &GameViewerDialog::onAnalyzeClicked);

    setCurrentPly(-1);
}

bool GameViewerDialog::setGame(const Meta& meta,
                              const FenTimelineResult& timeline,
                              QString* outError) {
    meta_ = meta;

    if (!timeline.ok) {
        if (outError) {
            *outError = QString::fromStdString(timeline.error);
        }
        return false;
    }

    startFen_ = !meta.startFen.trimmed().isEmpty()
                   ? meta.startFen.trimmed()
                   : QString::fromStdString(timeline.startFen);

    plies_.clear();
    plies_.reserve(timeline.plies.size());
    for (const auto& p : timeline.plies) {
        Ply o;
        o.plyIndex = p.plyIndex;
        o.san = p.san;
        o.uci = p.uci;
        o.fenAfter = p.fenAfter;
        o.posHashBefore = p.posHashBefore;
        plies_.push_back(std::move(o));
    }

    QString title = tr("Game viewer");
    if (!meta_.white.isEmpty() || !meta_.black.isEmpty()) {
        title = tr("Game viewer — %1 vs %2").arg(meta_.white, meta_.black);
    }
    setWindowTitle(title);

    QStringList headerLines;
    if (!meta_.event.isEmpty()) headerLines << tr("Event: %1").arg(meta_.event);
    if (!meta_.site.isEmpty())  headerLines << tr("Site: %1").arg(meta_.site);
    if (!meta_.date.isEmpty())  headerLines << tr("Date: %1").arg(meta_.date);
    if (!meta_.round.isEmpty()) headerLines << tr("Round: %1").arg(meta_.round);
    if (!meta_.white.isEmpty() || !meta_.black.isEmpty()) {
        headerLines << tr("Players: %1 - %2").arg(meta_.white, meta_.black);
    }
    if (!meta_.result.isEmpty()) headerLines << tr("Result: %1").arg(meta_.result);

    headerLabel_->setText(headerLines.join("\n"));

    rebuildMovesList();
    setCurrentPly(-1);

    return true;
}

void GameViewerDialog::rebuildMovesList() {
    ignoreSelection_ = true;
    movesList_->clear();

    for (size_t i = 0; i < plies_.size(); ++i) {
        const int ply = static_cast<int>(i);
        const int moveNo = (ply / 2) + 1;
        const bool isWhite = (ply % 2 == 0);
        const QString san = QString::fromStdString(plies_[i].san);

        QString text;
        if (isWhite) {
            text = QString("%1. %2").arg(moveNo).arg(san);
        } else {
            text = QString("%1... %2").arg(moveNo).arg(san);
        }

        auto* item = new QListWidgetItem(text);
        item->setData(Qt::UserRole, ply);
        movesList_->addItem(item);
    }

    ignoreSelection_ = false;
}

void GameViewerDialog::setCurrentPly(int plyIndex) {
    if (plyIndex < -1) plyIndex = -1;
    if (!plies_.empty()) {
        const int last = static_cast<int>(plies_.size()) - 1;
        if (plyIndex > last) plyIndex = last;
    } else {
        plyIndex = -1;
    }

    currentPly_ = plyIndex;

    if (boardWidget_) {
        boardWidget_->setFen(currentFen());
        boardWidget_->setArrows({});
        boardWidget_->setHighlights({});
    }

    ignoreSelection_ = true;
    if (movesList_) {
        if (currentPly_ >= 0) {
            movesList_->setCurrentRow(currentPly_);
        } else {
            movesList_->clearSelection();
            movesList_->setCurrentRow(-1);
        }
    }
    ignoreSelection_ = false;

    const bool hasMoves = !plies_.empty();
    const bool atStart = (currentPly_ <= -1);
    const bool atEnd = hasMoves && (currentPly_ >= static_cast<int>(plies_.size()) - 1);

    if (firstBtn_) firstBtn_->setEnabled(hasMoves && !atStart);
    if (prevBtn_)  prevBtn_->setEnabled(hasMoves && !atStart);
    if (nextBtn_)  nextBtn_->setEnabled(hasMoves && !atEnd);
    if (lastBtn_)  lastBtn_->setEnabled(hasMoves && !atEnd);
    if (analyzeBtn_) analyzeBtn_->setEnabled(true);
}

QString GameViewerDialog::currentFen() const {
    if (currentPly_ < 0) {
        return startFen_;
    }
    if (currentPly_ >= 0 && currentPly_ < static_cast<int>(plies_.size())) {
        return QString::fromStdString(plies_[static_cast<size_t>(currentPly_)].fenAfter);
    }
    return startFen_;
}

QString GameViewerDialog::opponentHint() const {
    if (!meta_.white.isEmpty() && !meta_.black.isEmpty()) {
        return tr("%1 vs %2").arg(meta_.white, meta_.black);
    }
    if (!meta_.black.isEmpty()) {
        return meta_.black;
    }
    if (!meta_.white.isEmpty()) {
        return meta_.white;
    }
    return QString();
}

void GameViewerDialog::onMoveSelectionChanged() {
    if (ignoreSelection_) {
        return;
    }
    const int row = movesList_ ? movesList_->currentRow() : -1;
    if (row < 0) {
        setCurrentPly(-1);
        return;
    }
    setCurrentPly(row);
}

void GameViewerDialog::onFirstClicked() {
    setCurrentPly(-1);
}

void GameViewerDialog::onPrevClicked() {
    setCurrentPly(currentPly_ - 1);
}

void GameViewerDialog::onNextClicked() {
    setCurrentPly(currentPly_ + 1);
}

void GameViewerDialog::onLastClicked() {
    if (plies_.empty()) {
        setCurrentPly(-1);
        return;
    }
    setCurrentPly(static_cast<int>(plies_.size()) - 1);
}

void GameViewerDialog::onAnalyzeClicked() {
    emit analyzeRequested(currentFen(), opponentHint());
}

} // namespace sf::client::ui
