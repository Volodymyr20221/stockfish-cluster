#include "ui/MainWindow.hpp"
#include "app/IHistoryRepository.hpp"
#include <algorithm>

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
#include <QSignalBlocker>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTableView>
#include <QItemSelectionModel>
#include <QTabWidget>
#include <QTextCursor>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>

#include <unordered_set>

#include "domain/domain_model.hpp"
#include "ui/BoardWidget.hpp"
#include "ui/JobExporter.hpp"
#include "infra/HistoryRepository.hpp"
#include <QTimeZone>

namespace sf::client::ui {

using sf::client::domain::Job;
using sf::client::domain::LimitType;
using sf::client::domain::SearchLimit;

MainWindow::MainWindow(sf::client::app::JobManager& jobManager,
                       sf::client::app::ServerManager& serverManager,
                       sf::client::app::IHistoryRepository* historyRepo,
                       QWidget* parent)
    : QMainWindow(parent)
    , jobsModel_(this)
    , serversModel_(this)
    , jobManager_(jobManager)
    , serverManager_(serverManager)
    , historyRepo_(historyRepo) {
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
    auto* formLayout = new QFormLayout();

    opponentLineEdit_ = new QLineEdit(centralWidget_);
    fenLineEdit_      = new QLineEdit(centralWidget_);

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
    formLayout->addRow(tr("FEN:"), fenLineEdit_);
    formLayout->addRow(tr("Limit type:"), limitTypeCombo_);
    formLayout->addRow(tr("Limit value:"), limitValueSpin_);
    formLayout->addRow(tr("MultiPV:"), multiPvSpin_);
    formLayout->addRow(tr("Server:"), serverCombo_);

    mainLayout->addLayout(formLayout);
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

void MainWindow::setupConnections() {
    connect(startButton_, &QPushButton::clicked,
            this, &MainWindow::onStartButtonClicked);
    connect(stopButton_, &QPushButton::clicked,
            this, &MainWindow::onStopButtonClicked);

    if (auto* selectionModel = jobsTableView_->selectionModel(); selectionModel) {
        connect(selectionModel, &QItemSelectionModel::selectionChanged,
                this, &MainWindow::onJobSelectionChanged);
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

void MainWindow::onStartButtonClicked() {
    const QString opponent = opponentLineEdit_->text().trimmed();
    const QString fen      = fenLineEdit_->text().trimmed();

    if (fen.isEmpty()) {
        QMessageBox::warning(
            this, tr("Validation error"),
            tr("FEN must not be empty."));
        return;
    }

    const int limitTypeInt  = limitTypeCombo_->currentData().toInt();
    const int limitValue    = limitValueSpin_->value();
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
    const QString serverId = serverCombo_->currentData().toString();
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
    const int maxArrows = std::clamp(job.multiPv, 1, 10);
        for (const auto& line : job.snapshot.lines) {
            if (added >= 3) break; // keep it readable
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

} // namespace sf::client::ui
