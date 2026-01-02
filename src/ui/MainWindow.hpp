#pragma once

#include <QMainWindow>
#include <optional>

#include "ui/JobsModel.hpp"
#include "ui/ServersModel.hpp"
#include "app/JobManager.hpp"
#include "app/ServerManager.hpp"

class QLineEdit;
class QSpinBox;
class QComboBox;
class QPushButton;
class QTableView;
class QPlainTextEdit;
class QTabWidget;
class QTimer;
class QItemSelectionModel;

namespace sf::client::app {
class IHistoryRepository;
}
class QVBoxLayout;
namespace sf::client::ui {

class BoardWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(sf::client::app::JobManager&     jobManager,
               sf::client::app::ServerManager&  serverManager,
               sf::client::app::IHistoryRepository* historyRepo = nullptr,
               QWidget* parent = nullptr);
    ~MainWindow() override;

    void notifyJobAddedOrUpdated(const sf::client::domain::Job& job);
    void notifyJobRemoved(const sf::client::domain::Job& job);

private slots:
    void onStartButtonClicked();
    void onStopButtonClicked();
    void onJobSelectionChanged();
    void exportJobsToJson();
    void exportJobsToPgn();

private:
    void setupUi();
    void setupMenu();
    void setupTopForm(QVBoxLayout* mainLayout);
    void setupButtonsRow(QVBoxLayout* mainLayout);
    void setupMainSplitter(QVBoxLayout* mainLayout);
    void setupServersTable(QVBoxLayout* mainLayout);
    void rebuildServerComboPreservingSelection();
    void setupConnections();
    void updateLogForSelectedRow(int row);
    void refreshServersTable();

    // Selection helpers (reduce branching / keep UI code on one abstraction level)
    QItemSelectionModel* jobsSelectionModel() const;
    std::optional<int> selectedJobRow() const;
    std::optional<sf::client::domain::Job> selectedJob() const;
    void selectJobRow(int row);
    void ensureLastRowSelectedIfNone();
    void refreshSelectedJobDetails();

    // UI detail view helpers (reduce branching / keep same abstraction level)
    void clearDetailsView();
    void showJobDetails(const sf::client::domain::Job& job);
    void updateLogView(const sf::client::domain::Job& job);
    void updatePvAndBoardView(const sf::client::domain::Job& job);


    std::vector<sf::client::domain::Job> collectJobsForExport() const;

private:
    QWidget*        centralWidget_{nullptr};

    QLineEdit*      opponentLineEdit_{nullptr};
    QLineEdit*      fenLineEdit_{nullptr};

    QComboBox*      limitTypeCombo_{nullptr};
    QSpinBox*       limitValueSpin_{nullptr};
    QSpinBox*       multiPvSpin_{nullptr};

    QComboBox*      serverCombo_{nullptr};

    QPushButton*    startButton_{nullptr};
    QPushButton*    stopButton_{nullptr};

    QTableView*     jobsTableView_{nullptr};
    QTabWidget*     detailsTabs_{nullptr};
    QPlainTextEdit* logPlainTextEdit_{nullptr};
    BoardWidget*    boardWidget_{nullptr};
    QPlainTextEdit* pvPlainTextEdit_{nullptr};

    QTableView*     serversTableView_{nullptr};

    JobsModel       jobsModel_;
    ServersModel    serversModel_;

    sf::client::app::JobManager&    jobManager_;
    sf::client::app::ServerManager& serverManager_;
    sf::client::app::IHistoryRepository* historyRepo_{nullptr};

    QTimer* serversRefreshTimer_{nullptr};
};

} // namespace sf::client::ui
