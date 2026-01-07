#include "ui/MainWindow.hpp"

#include "app/IHistoryRepository.hpp"
#include "app/IccfSyncManager.hpp"

#include <algorithm>
#include <unordered_set>

#include <QAction>
#include <QComboBox>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QOverload>
#include <QSignalBlocker>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTableView>
#include <QItemSelectionModel>
#include <QTabWidget>
#include <QTextCursor>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>

#include "domain/domain_model.hpp"
#include "domain/chess_san_to_fen.hpp"
#include "ui/BoardWidget.hpp"
#include "ui/JobExporter.hpp"
#include <QTimeZone>

namespace sf::client::ui {

using sf::client::domain::Job;
using sf::client::domain::LimitType;
using sf::client::domain::SearchLimit;

MainWindow::MainWindow(sf::client::app::JobManager& jobManager,
                       sf::client::app::ServerManager& serverManager,
                       sf::client::app::IHistoryRepository* historyRepo,
                       QWidget* parent)
    : MainWindow(jobManager, serverManager, historyRepo, /*iccfSync*/ nullptr, parent) {}

MainWindow::MainWindow(sf::client::app::JobManager& jobManager,
                       sf::client::app::ServerManager& serverManager,
                       sf::client::app::IHistoryRepository* historyRepo,
                       sf::client::app::IccfSyncManager* iccfSync,
                       QWidget* parent)
    : QMainWindow(parent)
    , jobsModel_(this)
    , serversModel_(this)
    , iccfGamesModel_(this)
    , jobManager_(jobManager)
    , serverManager_(serverManager)
    , historyRepo_(historyRepo)
    , iccfSync_(iccfSync) {
    setupUi();
    setupConnections();
    refreshServersTable();

    // Periodic refresh so server_status updates are reflected in UI.
    serversRefreshTimer_ = new QTimer(this);
    serversRefreshTimer_->setInterval(1000);
    connect(serversRefreshTimer_, &QTimer::timeout,
            this, &MainWindow::refreshServersTable);
    serversRefreshTimer_->start();
}

MainWindow::~MainWindow() = default;

void MainWindow::setupUi() {
    resize(1200, 700);
    setWindowTitle(tr("Stockfish cluster client"));

    setupMenu();

    centralWidget_ = new QWidget(this);
    setCentralWidget(centralWidget_);

    auto* mainLayout = new QVBoxLayout(centralWidget_);
    setupTopForm(mainLayout);
    setupButtonsRow(mainLayout);
    setupMainSplitter(mainLayout);
    setupServersTable(mainLayout);
}

void MainWindow::setupMenu() {
    auto* fileMenu = menuBar()->addMenu(tr("&File"));

    auto* exportJsonAction =
        fileMenu->addAction(tr("Export jobs to JSON"));
    connect(exportJsonAction, &QAction::triggered,
            this, &MainWindow::exportJobsToJson);

    auto* exportPgnAction =
        fileMenu->addAction(tr("Export jobs to PGN"));
    connect(exportPgnAction, &QAction::triggered,
            this, &MainWindow::exportJobsToPgn);
}

void MainWindow::setupTopForm(QVBoxLayout* mainLayout) {
    topFormLayout_ = new QFormLayout();
    auto* formLayout = topFormLayout_;

    opponentLineEdit_ = new QLineEdit(centralWidget_);

    positionInputCombo_ = new QComboBox(centralWidget_);
    positionInputCombo_->addItem(tr("FEN"), QVariant(QStringLiteral("fen")));
    positionInputCombo_->addItem(tr("Moves (SAN/PGN)"), QVariant(QStringLiteral("moves")));

    fenLineEdit_ = new QLineEdit(centralWidget_);
    fenLineEdit_->setPlaceholderText(
        tr("e.g. rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"));

    movesPlainTextEdit_ = new QPlainTextEdit(centralWidget_);
    movesPlainTextEdit_->setPlaceholderText(
        tr("Paste moves like: 1.d4 d5 2.c4 e6 3.Nf3 ..."));
    movesPlainTextEdit_->setMinimumHeight(60);
    movesPlainTextEdit_->setMaximumHeight(130);

    limitTypeCombo_ = new QComboBox(centralWidget_);
    limitTypeCombo_->addItem(tr("Depth"),
                             QVariant(static_cast<int>(LimitType::Depth)));
    limitTypeCombo_->addItem(tr("Time (ms)"),
                             QVariant(static_cast<int>(LimitType::TimeMs)));
    limitTypeCombo_->addItem(tr("Nodes"),
                             QVariant(static_cast<int>(LimitType::Nodes)));

    limitValueSpin_ = new QSpinBox(centralWidget_);
    limitValueSpin_->setRange(1, 1'000'000);
    limitValueSpin_->setValue(30);

    multiPvSpin_ = new QSpinBox(centralWidget_);
    multiPvSpin_->setRange(1, 10);
    multiPvSpin_->setValue(1);

    serverCombo_ = new QComboBox(centralWidget_);
    serverCombo_->addItem(tr("Auto"), QVariant(QString()));

    formLayout->addRow(tr("Opponent:"), opponentLineEdit_);
    formLayout->addRow(tr("Position input:"), positionInputCombo_);
    formLayout->addRow(tr("FEN:"), fenLineEdit_);
    formLayout->addRow(tr("Moves:"), movesPlainTextEdit_);
    formLayout->addRow(tr("Limit type:"), limitTypeCombo_);
    formLayout->addRow(tr("Limit value:"), limitValueSpin_);
    formLayout->addRow(tr("MultiPV:"), multiPvSpin_);
    formLayout->addRow(tr("Server:"), serverCombo_);

    mainLayout->addLayout(formLayout);

    // Default mode is FEN
    updatePositionInputModeUi();
}

void MainWindow::setupButtonsRow(QVBoxLayout* mainLayout) {
    auto* buttonsLayout = new QHBoxLayout();
    startButton_ = new QPushButton(tr("Start"), centralWidget_);
    stopButton_  = new QPushButton(tr("Stop"), centralWidget_);
    buttonsLayout->addWidget(startButton_);
    buttonsLayout->addWidget(stopButton_);
    buttonsLayout->addStretch();
    mainLayout->addLayout(buttonsLayout);
}

void MainWindow::setupMainSplitter(QVBoxLayout* mainLayout) {
    auto* splitter = new QSplitter(Qt::Horizontal, centralWidget_);

    jobsTableView_ = new QTableView(splitter);
    jobsTableView_->setModel(&jobsModel_);
    jobsTableView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    jobsTableView_->setSelectionMode(QAbstractItemView::SingleSelection);
    jobsTableView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    jobsTableView_->horizontalHeader()->setStretchLastSection(true);

    detailsTabs_ = new QTabWidget(splitter);

    logPlainTextEdit_ = new QPlainTextEdit(detailsTabs_);
    logPlainTextEdit_->setReadOnly(true);
    detailsTabs_->addTab(logPlainTextEdit_, tr("Log"));

    // Board tab: board + PV lines
    auto* boardTab = new QWidget(detailsTabs_);
    auto* boardLayout = new QVBoxLayout(boardTab);
    boardLayout->setContentsMargins(0, 0, 0, 0);
    boardWidget_ = new BoardWidget(boardTab);

    pvPlainTextEdit_ = new QPlainTextEdit(boardTab);
    pvPlainTextEdit_->setReadOnly(true);
    pvPlainTextEdit_->setMaximumBlockCount(2000);
    pvPlainTextEdit_->setMinimumHeight(90);

    boardLayout->addWidget(boardWidget_, 1);
    boardLayout->addWidget(pvPlainTextEdit_, 0);
    detailsTabs_->addTab(boardTab, tr("Board"));

    // ICCF tab
    setupIccfTab();

    splitter->addWidget(jobsTableView_);
    splitter->addWidget(detailsTabs_);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);

    mainLayout->addWidget(splitter, 1);
}

void MainWindow::setupServersTable(QVBoxLayout* mainLayout) {
    serversTableView_ = new QTableView(centralWidget_);
    serversTableView_->setModel(&serversModel_);
    serversTableView_->setSelectionMode(QAbstractItemView::NoSelection);
    serversTableView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    serversTableView_->horizontalHeader()->setStretchLastSection(true);
    serversTableView_->setMinimumHeight(150);

    mainLayout->addWidget(serversTableView_);
}

void MainWindow::setupIccfTab() {
    if (!detailsTabs_) return;

    auto* tab = new QWidget(detailsTabs_);
    auto* layout = new QVBoxLayout(tab);
    layout->setContentsMargins(6, 6, 6, 6);

    auto* form = new QFormLayout();

    iccfEndpointLineEdit_ = new QLineEdit(tab);
    iccfEndpointLineEdit_->setText(QStringLiteral("https://www.iccf.com/XfccBasic.asmx"));

    iccfUsernameLineEdit_ = new QLineEdit(tab);
    iccfPasswordLineEdit_ = new QLineEdit(tab);
    iccfPasswordLineEdit_->setEchoMode(QLineEdit::Password);

    form->addRow(tr("Endpoint:"), iccfEndpointLineEdit_);
    form->addRow(tr("Username:"), iccfUsernameLineEdit_);
    form->addRow(tr("Password:"), iccfPasswordLineEdit_);
    layout->addLayout(form);

    auto* buttonsLayout = new QHBoxLayout();
    iccfRefreshButton_ = new QPushButton(tr("Refresh"), tab);
    iccfAnalyzeButton_ = new QPushButton(tr("Analyze selected"), tab);
    buttonsLayout->addWidget(iccfRefreshButton_);
    buttonsLayout->addWidget(iccfAnalyzeButton_);
    buttonsLayout->addStretch(1);
    layout->addLayout(buttonsLayout);

    iccfGamesTableView_ = new QTableView(tab);
    iccfGamesTableView_->setModel(&iccfGamesModel_);
    iccfGamesTableView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    iccfGamesTableView_->setSelectionMode(QAbstractItemView::SingleSelection);
    iccfGamesTableView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    iccfGamesTableView_->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(iccfGamesTableView_, 1);

    // If ICCF not wired, keep UI visible but disable actions.
    if (!iccfSync_) {
        iccfRefreshButton_->setEnabled(false);
        iccfAnalyzeButton_->setEnabled(false);
        iccfUsernameLineEdit_->setPlaceholderText(tr("ICCF is not wired (use constructor overload with IccfSyncManager*)"));
    }

    detailsTabs_->addTab(tab, tr("ICCF"));
}

void MainWindow::setupConnections() {
    connect(startButton_, &QPushButton::clicked,
            this, &MainWindow::onStartButtonClicked);
    connect(stopButton_, &QPushButton::clicked,
            this, &MainWindow::onStopButtonClicked);

    if (positionInputCombo_) {
        connect(positionInputCombo_,
                QOverload<int>::of(&QComboBox::currentIndexChanged),
                this,
                &MainWindow::updatePositionInputModeUi);
    }

    if (auto* selectionModel = jobsTableView_->selectionModel(); selectionModel) {
        connect(selectionModel, &QItemSelectionModel::selectionChanged,
                this, &MainWindow::onJobSelectionChanged);
    }

    // ICCF
    if (iccfRefreshButton_) {
        connect(iccfRefreshButton_, &QPushButton::clicked,
                this, &MainWindow::onIccfRefreshClicked);
    }
    if (iccfAnalyzeButton_) {
        connect(iccfAnalyzeButton_, &QPushButton::clicked,
                this, &MainWindow::onIccfAnalyzeClicked);
    }

    if (iccfSync_) {
        connect(iccfSync_, &sf::client::app::IccfSyncManager::gamesUpdated,
                this, &MainWindow::onIccfGamesUpdated);
        connect(iccfSync_, &sf::client::app::IccfSyncManager::error,
                this, &MainWindow::onIccfError);
    }
}

QItemSelectionModel* MainWindow::jobsSelectionModel() const {
    return jobsTableView_ ? jobsTableView_->selectionModel() : nullptr;
}

std::optional<int> MainWindow::selectedJobRow() const {
    auto* selectionModel = jobsSelectionModel();
    if (!selectionModel) {
        return std::nullopt;
    }

    const auto selected = selectionModel->selectedRows();
    if (selected.isEmpty()) {
        return std::nullopt;
    }
    return selected.first().row();
}

std::optional<Job> MainWindow::selectedJob() const {
    const auto row = selectedJobRow();
    if (!row.has_value()) {
        return std::nullopt;
    }
    return jobsModel_.jobAtRow(*row);
}

void MainWindow::selectJobRow(int row) {
    auto* selectionModel = jobsSelectionModel();
    if (!selectionModel) {
        return;
    }

    selectionModel->select(jobsModel_.index(row, 0),
                           QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}

void MainWindow::ensureLastRowSelectedIfNone() {
    if (selectedJobRow().has_value()) {
        return;
    }
    if (jobsModel_.rowCount() <= 0) {
        return;
    }

    const int row = jobsModel_.rowCount() - 1;
    selectJobRow(row);
    updateLogForSelectedRow(row);
}

void MainWindow::refreshSelectedJobDetails() {
    const auto row = selectedJobRow();
    if (!row.has_value()) {
        clearDetailsView();
        return;
    }
    updateLogForSelectedRow(*row);
}

void MainWindow::notifyJobAddedOrUpdated(const Job& job) {
    jobsModel_.upsertJob(job);

    // If nothing selected yet, auto-select the last row (helps UX for first run).
    ensureLastRowSelectedIfNone();

    // If the updated job is currently selected -> live-update details view.
    const auto current = selectedJob();
    if (current && current->id == job.id) {
        refreshSelectedJobDetails();
    }
}

void MainWindow::notifyJobRemoved(const Job& job) {
    jobsModel_.removeJob(job.id);

    // Selection might become empty or point to a different row after removal.
    refreshSelectedJobDetails();
}

void MainWindow::rebuildServerComboPreservingSelection() {
    if (!serverCombo_) {
        return;
    }

    const QString prevId = serverCombo_->currentData().toString();
    QSignalBlocker blocker(serverCombo_);

    serverCombo_->clear();
    serverCombo_->addItem(tr("Auto"), QVariant(QString()));

    for (const auto& s : serverManager_.servers()) {
        if (!s.enabled) {
            continue;
        }
        serverCombo_->addItem(QString::fromStdString(s.name),
                              QVariant(QString::fromStdString(s.id)));
    }

    const int idx = !prevId.isEmpty() ? serverCombo_->findData(prevId) : 0;
    serverCombo_->setCurrentIndex(idx >= 0 ? idx : 0);
}

void MainWindow::refreshServersTable() {
    serversModel_.setServers(serverManager_.servers());
    rebuildServerComboPreservingSelection();
}

void MainWindow::updatePositionInputModeUi() {
    if (!topFormLayout_ || !positionInputCombo_ || !fenLineEdit_ || !movesPlainTextEdit_) {
        return;
    }

    const QString mode = positionInputCombo_->currentData().toString();
    const bool useMoves = (mode == QStringLiteral("moves"));

    topFormLayout_->setRowVisible(fenLineEdit_, !useMoves);
    topFormLayout_->setRowVisible(movesPlainTextEdit_, useMoves);
}

void MainWindow::onStartButtonClicked() {
    const QString opponent = opponentLineEdit_ ? opponentLineEdit_->text().trimmed() : QString();

    QString fen;
    const QString mode = positionInputCombo_
                             ? positionInputCombo_->currentData().toString()
                             : QStringLiteral("fen");

    if (mode == QStringLiteral("moves")) {
        const QString moves = movesPlainTextEdit_
                                  ? movesPlainTextEdit_->toPlainText().trimmed()
                                  : QString();

        if (moves.isEmpty()) {
            QMessageBox::warning(this, tr("Validation error"),
                                 tr("Moves must not be empty."));
            return;
        }

        const auto res = sf::client::domain::chess::fenFromSanMoves(moves.toStdString(), std::nullopt);
        if (!res.ok) {
            QMessageBox::warning(
                this, tr("Moves parse error"),
                tr("Failed to parse/apply moves.\n\n%1").arg(QString::fromStdString(res.error)));
            return;
        }

        fen = QString::fromStdString(res.fen);
    } else {
        fen = fenLineEdit_ ? fenLineEdit_->text().trimmed() : QString();
        if (fen.isEmpty()) {
            QMessageBox::warning(this, tr("Validation error"),
                                 tr("FEN must not be empty."));
            return;
        }
    }

    const int limitTypeInt  = limitTypeCombo_ ? limitTypeCombo_->currentData().toInt()
                                              : static_cast<int>(LimitType::Depth);
    const int limitValue    = limitValueSpin_ ? limitValueSpin_->value() : 30;
    SearchLimit limit;

    switch (static_cast<LimitType>(limitTypeInt)) {
        case LimitType::Depth:
            limit = sf::client::domain::depth(limitValue);
            break;
        case LimitType::TimeMs:
            limit = sf::client::domain::movetime_ms(limitValue);
            break;
        case LimitType::Nodes:
            limit = sf::client::domain::nodes(limitValue);
            break;
    }

    std::optional<std::string> preferredServer;
    const QString serverId = serverCombo_ ? serverCombo_->currentData().toString() : QString();
    if (!serverId.isEmpty()) {
        preferredServer = serverId.toStdString();
    }

    const int multiPv = multiPvSpin_ ? multiPvSpin_->value() : 1;

    jobManager_.enqueueJob(opponent.toStdString(),
                           fen.toStdString(),
                           limit,
                           multiPv,
                           preferredServer);
}

void MainWindow::onStopButtonClicked() {
    const auto job = selectedJob();
    if (!job) {
        return;
    }
    jobManager_.requestStopJob(job->id);
}

void MainWindow::onJobSelectionChanged() {
    refreshSelectedJobDetails();
}

void MainWindow::updateLogForSelectedRow(int row) {
    const auto optJob = jobsModel_.jobAtRow(row);
    if (!optJob.has_value()) {
        clearDetailsView();
        return;
    }
    showJobDetails(optJob.value());
}

void MainWindow::clearDetailsView() {
    if (logPlainTextEdit_) {
        logPlainTextEdit_->clear();
    }
    if (pvPlainTextEdit_) {
        pvPlainTextEdit_->clear();
    }
    if (boardWidget_) {
        boardWidget_->setFen(QString());
        boardWidget_->setArrows({});
        boardWidget_->setHighlights({});
    }
}

void MainWindow::showJobDetails(const Job& job) {
    updateLogView(job);
    updatePvAndBoardView(job);
}

void MainWindow::updateLogView(const Job& job) {
    QStringList lines;
    lines.reserve(static_cast<int>(job.logLines.size()));
    for (const auto& l : job.logLines) {
        lines.append(QString::fromStdString(l));
    }
    logPlainTextEdit_->setPlainText(lines.join('\n'));
    logPlainTextEdit_->moveCursor(QTextCursor::End);
}

void MainWindow::updatePvAndBoardView(const Job& job) {
    if (pvPlainTextEdit_) {
        pvPlainTextEdit_->clear();
    }
    if (!boardWidget_) {
        return;
    }

    boardWidget_->setFen(QString::fromStdString(job.fen));

    QVector<sf::client::ui::Arrow> arrows;
    QVector<sf::client::ui::Square> hl;

    // PV text box
    if (pvPlainTextEdit_) {
        QStringList pvLines;
        if (!job.snapshot.lines.empty()) {
            for (const auto& line : job.snapshot.lines) {
                QString scoreStr;
                if (line.score.type == sf::client::domain::ScoreType::Cp) {
                    scoreStr = QString::number(line.score.value) + " cp";
                } else if (line.score.type == sf::client::domain::ScoreType::Mate) {
                    scoreStr = "M" + QString::number(line.score.value);
                }
                const QString pv = QString::fromStdString(line.pv);
                pvLines << QString("#%1  %2  %3").arg(line.multipv).arg(scoreStr, pv);
            }
        } else {
            const QString pv = QString::fromStdString(job.snapshot.pv);
            if (!pv.trimmed().isEmpty()) {
                pvLines << pv;
            }
        }
        pvPlainTextEdit_->setPlainText(pvLines.join('\n'));
    }

    // Helper: create arrow from first move in PV (UCI)
    const auto makeArrow = [&](const QString& uciMove,
                               const sf::client::domain::Score& score,
                               int multipv) {
        if (uciMove.trimmed().isEmpty()) {
            return;
        }
        std::optional<int> cp;
        std::optional<int> mate;
        if (score.type == sf::client::domain::ScoreType::Cp) {
            cp = score.value;
        } else if (score.type == sf::client::domain::ScoreType::Mate) {
            mate = score.value;
        }

        auto a = BoardWidget::arrowFromUciMove(uciMove, cp, mate, multipv);
        if (a.has_value()) {
            arrows.push_back(*a);
            if (multipv == 1) {
                hl.push_back(a->from);
                hl.push_back(a->to);
            }
        }
    };

    // Prefer MultiPV lines, otherwise fallback to bestmove/pv.
    if (!job.snapshot.lines.empty()) {
        int added = 0;
        const int maxArrows = std::min(3, std::clamp(job.multiPv, 1, 10));
        for (const auto& line : job.snapshot.lines) {
            if (added >= maxArrows) break; // keep it readable
            const QString pv = QString::fromStdString(line.pv).trimmed();
            const QString first = pv.split(' ', Qt::SkipEmptyParts).value(0);
            makeArrow(first, line.score, line.multipv);
            if (!first.isEmpty()) {
                added++;
            }
        }
    } else {
        QString uci = QString::fromStdString(job.snapshot.bestMove).trimmed();
        if (uci.isEmpty()) {
            const QString pv = QString::fromStdString(job.snapshot.pv).trimmed();
            if (!pv.isEmpty()) {
                uci = pv.split(' ', Qt::SkipEmptyParts).value(0);
            }
        }
        makeArrow(uci, job.snapshot.score, 1);
    }

    boardWidget_->setArrows(arrows);
    boardWidget_->setHighlights(hl);
}

std::vector<Job> MainWindow::collectJobsForExport() const {
    std::vector<Job> jobs;

    // 1) History (terminal jobs)
    if (historyRepo_) {
        jobs = historyRepo_->loadAllJobs();
    }

    // Track existing IDs so we can append only new in-memory jobs (preserve order).
    std::unordered_set<std::string> seen;
    seen.reserve(jobs.size() + jobManager_.jobs().size());
    for (const auto& j : jobs) {
        seen.insert(j.id);
    }

    // 2) Active jobs (in-memory)
    const auto& active = jobManager_.jobs();
    for (const auto& j : active) {
        if (seen.insert(j.id).second) {
            jobs.push_back(j);
        }
    }

    return jobs;
}

void MainWindow::exportJobsToJson() {
    const auto jobs = collectJobsForExport();

    const QString fileName = QFileDialog::getSaveFileName(
        this,
        tr("Export jobs as JSON"),
        QString(),
        tr("JSON files (*.json)"));
    if (fileName.isEmpty()) {
        return;
    }

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Error"),
                             tr("Failed to open file for writing."));
        return;
    }

    const QJsonDocument doc = JobExporter::toJson(jobs);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
}

void MainWindow::exportJobsToPgn() {
    const auto jobs = collectJobsForExport();

    const QString fileName = QFileDialog::getSaveFileName(
        this,
        tr("Export jobs as PGN"),
        QString(),
        tr("PGN files (*.pgn);;All files (*.*)"));
    if (fileName.isEmpty()) {
        return;
    }

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Error"),
                             tr("Failed to open file for writing."));
        return;
    }

    const QString pgn = JobExporter::toPgn(jobs);
    file.write(pgn.toUtf8());
    file.close();
}

// ---------------- ICCF slots ----------------

void MainWindow::onIccfRefreshClicked() {
    if (!iccfSync_) {
        QMessageBox::information(this, tr("ICCF"),
                                 tr("ICCF is not wired. Use the constructor overload with IccfSyncManager*."));
        return;
    }

    sf::client::app::IccfSyncManager::Credentials c;
    c.endpointUrl = iccfEndpointLineEdit_ ? iccfEndpointLineEdit_->text().trimmed() : QString();
    c.username = iccfUsernameLineEdit_ ? iccfUsernameLineEdit_->text().trimmed() : QString();
    c.password = iccfPasswordLineEdit_ ? iccfPasswordLineEdit_->text() : QString();

    iccfSync_->setCredentials(std::move(c));
    iccfSync_->refreshNow();
}

void MainWindow::onIccfAnalyzeClicked() {
    if (!iccfGamesTableView_) return;

    const auto selected = iccfGamesTableView_->selectionModel()
                              ? iccfGamesTableView_->selectionModel()->selectedRows()
                              : QModelIndexList{};
    if (selected.isEmpty()) {
        QMessageBox::information(this, tr("ICCF"), tr("Select a game first."));
        return;
    }

    const int row = selected.first().row();
    const auto* g = iccfGamesModel_.gameAt(row);
    if (!g) {
        QMessageBox::warning(this, tr("ICCF"), tr("Invalid selection."));
        return;
    }

    const QString label = QStringLiteral("ICCF #%1: %2 vs %3")
                              .arg(g->id)
                              .arg(g->white)
                              .arg(g->black);

    std::optional<std::string> startFen;
    if (g->setup && !g->fen.trimmed().isEmpty()) {
        startFen = g->fen.toStdString();
    }

    const auto res = sf::client::domain::chess::fenFromSanMoves(g->moves.toStdString(), startFen);
    if (!res.ok) {
        QMessageBox::warning(this, tr("ICCF"),
                             tr("Failed to parse/apply ICCF moves.\n\n%1")
                                 .arg(QString::fromStdString(res.error)));
        return;
    }

    const int limitTypeInt  = limitTypeCombo_ ? limitTypeCombo_->currentData().toInt()
                                              : static_cast<int>(LimitType::Depth);
    const int limitValue    = limitValueSpin_ ? limitValueSpin_->value() : 30;

    SearchLimit limit;
    switch (static_cast<LimitType>(limitTypeInt)) {
        case LimitType::Depth:  limit = sf::client::domain::depth(limitValue); break;
        case LimitType::TimeMs: limit = sf::client::domain::movetime_ms(limitValue); break;
        case LimitType::Nodes:  limit = sf::client::domain::nodes(limitValue); break;
    }

    std::optional<std::string> preferredServer;
    const QString serverId = serverCombo_ ? serverCombo_->currentData().toString() : QString();
    if (!serverId.isEmpty()) {
        preferredServer = serverId.toStdString();
    }

    const int multiPv = multiPvSpin_ ? multiPvSpin_->value() : 1;

    jobManager_.enqueueJob(label.toStdString(),
                           res.fen,
                           limit,
                           multiPv,
                           preferredServer);
}

void MainWindow::onIccfGamesUpdated(QVector<sf::client::infra::iccf::IccfGame> games) {
    iccfGamesModel_.setGames(std::move(games));
    if (statusBar()) {
        statusBar()->showMessage(tr("ICCF games updated."), 3000);
    }
}

void MainWindow::onIccfError(const QString& message) {
    if (statusBar()) {
        statusBar()->showMessage(message, 6000);
    }
    QMessageBox::warning(this, tr("ICCF"), message);
}

} // namespace sf::client::ui
