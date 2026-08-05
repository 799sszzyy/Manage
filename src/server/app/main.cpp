#include "manage/data/database_config.h"
#include "manage/data/database_connection.h"
#include "manage/data/bom_service.h"
#include "manage/data/migration_runner.h"
#include "manage/data/mysql_bom_repository.h"
#include "manage/server/api_server.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QHostAddress>
#include <QTimer>

#include <memory>

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("manage-server"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Local REST API for the quotation management system")
    );
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption portOption(
        QStringList{QStringLiteral("p"), QStringLiteral("port")},
        QStringLiteral("TCP port; the service always binds to 127.0.0.1"),
        QStringLiteral("port"),
        QStringLiteral("18080")
    );
    const QCommandLineOption smokeTestOption(
        QStringLiteral("smoke-test"),
        QStringLiteral("Start on an ephemeral port and exit immediately")
    );
    const QCommandLineOption migrateOnlyOption(
        QStringLiteral("migrate-only"),
        QStringLiteral("Apply database migrations and exit")
    );
    parser.addOption(portOption);
    parser.addOption(smokeTestOption);
    parser.addOption(migrateOnlyOption);
    parser.process(application);

    if (parser.isSet(smokeTestOption) && parser.isSet(migrateOnlyOption)) {
        qCritical("--smoke-test and --migrate-only cannot be used together");
        return 2;
    }

    bool portIsValid = false;
    const auto parsedPort = parser.value(portOption).toUInt(&portIsValid);
    if (!portIsValid || parsedPort > 65'535 || parsedPort == 0) {
        qCritical("--port must be between 1 and 65535");
        return 2;
    }

    std::unique_ptr<manage::data::DatabaseConnection> databaseConnection;
    std::unique_ptr<manage::data::MySqlBomRepository> bomRepository;
    std::unique_ptr<manage::data::BomService> bomService;
    if (!parser.isSet(smokeTestOption)) {
        databaseConnection = std::make_unique<manage::data::DatabaseConnection>(
            manage::data::DatabaseConfig::fromEnvironment()
        );

        QString databaseError;
        if (!databaseConnection->open(&databaseError)) {
            qCritical().noquote() << "Database connection failed:" << databaseError;
            return 3;
        }

        manage::data::MigrationRunner migrationRunner(
            databaseConnection->database()
        );
        manage::data::MigrationReport migrationReport;
        if (!migrationRunner.migrate(&migrationReport, &databaseError)) {
            qCritical().noquote() << "Database migration failed:" << databaseError;
            return 4;
        }
        qInfo(
            "database schema ready at version %d (%d applied)",
            migrationReport.currentVersion,
            migrationReport.appliedCount
        );

        if (parser.isSet(migrateOnlyOption)) {
            return 0;
        }

        bomRepository = std::make_unique<manage::data::MySqlBomRepository>(
            databaseConnection->database()
        );
        bomService = std::make_unique<manage::data::BomService>(*bomRepository);
    }

    manage::server::ApiServer server(bomService.get());
    const auto requestedPort = parser.isSet(smokeTestOption)
                                   ? static_cast<quint16>(0)
                                   : static_cast<quint16>(parsedPort);
    const auto listeningPort = server.listen(
        QHostAddress::LocalHost,
        requestedPort
    );
    if (listeningPort == 0) {
        qCritical("Unable to listen on 127.0.0.1");
        return 1;
    }

    qInfo("manage-server listening on http://127.0.0.1:%u", listeningPort);

    if (parser.isSet(smokeTestOption)) {
        QTimer::singleShot(50, &application, &QCoreApplication::quit);
    }

    return application.exec();
}
