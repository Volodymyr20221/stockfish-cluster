#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QString>

#include "infra/ServerConfigRepository.hpp"
#include "infra/HistoryRepository.hpp"
#include "app/ServerManager.hpp"
#include "app/JobManager.hpp"
#include "net/JobNetworkController.hpp"
#include "ui/MainWindow.hpp"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    const QString appDir = QCoreApplication::applicationDirPath();
    qDebug() << "Application dir:" << appDir;

    // Help Qt find plugins shipped next to the exe (e.g. sqldrivers/qsqlite.dll).
    QCoreApplication::addLibraryPath(appDir);
    QCoreApplication::addLibraryPath(appDir + "/plugins");
    QCoreApplication::addLibraryPath(appDir + "/sqldrivers");

    const QString serversPath = appDir + "/servers.json";
    const QString dbPath      = appDir + "/history.sqlite";

    qDebug() << "Servers config path:" << serversPath;
    qDebug() << "History DB path:" << dbPath;

    sf::client::infra::ServerConfigRepository configRepo(serversPath.toStdString());
    const auto servers = configRepo.load();

    sf::client::app::ServerManager serverManager(servers);

    sf::client::infra::HistoryRepository historyRepo(dbPath);
    sf::client::app::JobManager jobManager(serverManager, &historyRepo);

    sf::client::net::JobNetworkController netController(jobManager, serverManager);
    netController.initializeConnections(serverManager.servers());

    sf::client::ui::MainWindow w(jobManager, serverManager, &historyRepo);
    w.show();

    sf::client::app::JobManagerCallbacks cb;
    cb.onJobAdded = [&](const sf::client::domain::Job& job) {
        w.notifyJobAddedOrUpdated(job);
        netController.handleJobAddedOrUpdated(job);
    };
    cb.onJobUpdated = [&](const sf::client::domain::Job& job) {
        // UI must update on every change, including remote progress updates.
        w.notifyJobAddedOrUpdated(job);

        // IMPORTANT:
        // Do NOT echo remote job_update back to the server (would create a feedback loop).
        // We only send a cancel request when the user stops a job locally.
        if (job.status == sf::client::domain::JobStatus::Stopped) {
            netController.handleJobRemoved(job); // sends job_cancel
        }
    };
    cb.onJobRemoved = [&](const sf::client::domain::Job& job) {
        w.notifyJobRemoved(job);
        netController.handleJobRemoved(job);
    };

    jobManager.setCallbacks(std::move(cb));

    return app.exec();
}
