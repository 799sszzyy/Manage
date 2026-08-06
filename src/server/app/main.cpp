#include "manage/auth/auth_service.h"
#include "manage/auth/user_management.h"
#include "manage/data/bom_service.h"
#include "manage/data/catalog_repository.h"
#include "manage/data/database_config.h"
#include "manage/data/database_connection.h"
#include "manage/data/migration_runner.h"
#include "manage/data/material_batch_service.h"
#include "manage/data/mysql_bom_repository.h"
#include "manage/data/mysql_quote_lifecycle.h"
#include "manage/data/mysql_statistics_repository.h"
#include "manage/data/mysql_user_repository.h"
#include "manage/data/process_step_repository.h"
#include "manage/data/process_step_service.h"
#include "manage/data/task_repository.h"
#include "manage/data/task_service.h"
#include "manage/server/api_server.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QAbstractSocket>
#include <QHostAddress>
#include <QTextStream>
#include <QTimer>

#include <memory>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

QString readDatabasePassword() {
    QTextStream output(stdout);
    output << "Enter the MySQL password: " << Qt::flush;

#ifdef Q_OS_WIN
    const auto inputHandle = GetStdHandle(STD_INPUT_HANDLE);
    DWORD originalMode = 0;
    const auto hasConsoleMode = inputHandle != INVALID_HANDLE_VALUE &&
                                GetConsoleMode(inputHandle, &originalMode);
    if (hasConsoleMode) {
        SetConsoleMode(inputHandle, originalMode & ~ENABLE_ECHO_INPUT);
    }
#endif

    QTextStream input(stdin);
    const auto password = input.readLine();

#ifdef Q_OS_WIN
    if (hasConsoleMode) {
        SetConsoleMode(inputHandle, originalMode);
    }
#endif

    output << Qt::endl;
    return password;
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("manage-server"));
    QCoreApplication::setApplicationVersion(QStringLiteral(MANAGE_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Local REST API for the quotation management system")
    );
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption portOption(
        QStringList{QStringLiteral("p"), QStringLiteral("port")},
        QStringLiteral("TCP port"),
        QStringLiteral("port"),
        QStringLiteral("18080")
    );
    const QCommandLineOption listenAddressOption(
        QStringLiteral("listen-address"),
        QStringLiteral("IPv4 address to listen on; defaults to local-only access"),
        QStringLiteral("address"),
        QStringLiteral("127.0.0.1")
    );
    const QCommandLineOption allowLanOption(
        QStringLiteral("allow-lan"),
        QStringLiteral("Explicitly allow listening on a non-loopback address")
    );
    const QCommandLineOption smokeTestOption(
        QStringLiteral("smoke-test"),
        QStringLiteral("Start on an ephemeral port and exit immediately")
    );
    const QCommandLineOption migrateOnlyOption(
        QStringLiteral("migrate-only"),
        QStringLiteral("Apply database migrations and exit")
    );
    const QCommandLineOption databaseHostOption(
        QStringLiteral("db-host"),
        QStringLiteral("MySQL host; overrides MANAGE_DB_HOST"),
        QStringLiteral("host")
    );
    const QCommandLineOption databasePortOption(
        QStringLiteral("db-port"),
        QStringLiteral("MySQL port; overrides MANAGE_DB_PORT"),
        QStringLiteral("port")
    );
    const QCommandLineOption databaseNameOption(
        QStringLiteral("db-name"),
        QStringLiteral("MySQL database; overrides MANAGE_DB_NAME"),
        QStringLiteral("name")
    );
    const QCommandLineOption databaseUserOption(
        QStringLiteral("db-user"),
        QStringLiteral("MySQL user; overrides MANAGE_DB_USER"),
        QStringLiteral("user")
    );
    const QCommandLineOption promptDatabasePasswordOption(
        QStringLiteral("prompt-db-password"),
        QStringLiteral("Read the MySQL password from the visible console without echo")
    );
    parser.addOption(portOption);
    parser.addOption(listenAddressOption);
    parser.addOption(allowLanOption);
    parser.addOption(smokeTestOption);
    parser.addOption(migrateOnlyOption);
    parser.addOption(databaseHostOption);
    parser.addOption(databasePortOption);
    parser.addOption(databaseNameOption);
    parser.addOption(databaseUserOption);
    parser.addOption(promptDatabasePasswordOption);
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

    QHostAddress listenAddress;
    const auto listenAddressText = parser.value(listenAddressOption).trimmed();
    if (!listenAddress.setAddress(listenAddressText) ||
        listenAddress.protocol() != QAbstractSocket::IPv4Protocol ||
        listenAddress == QHostAddress::Broadcast || listenAddress.isMulticast()) {
        qCritical("--listen-address must be a valid unicast IPv4 address");
        return 2;
    }
    const auto lanMode = !listenAddress.isLoopback();
    if (lanMode && !parser.isSet(allowLanOption)) {
        qCritical(
            "a non-loopback --listen-address requires the explicit --allow-lan flag"
        );
        return 2;
    }
    if (lanMode) {
        qWarning(
            "LAN mode enabled: keep port %u inside a trusted Windows network; "
            "the built-in HTTP service does not provide TLS",
            parsedPort
        );
    }

    std::unique_ptr<manage::data::DatabaseConnection> databaseConnection;
    std::shared_ptr<manage::auth::AuthService> authService;
    std::shared_ptr<manage::auth::UserManagementService> userManagementService;
    std::shared_ptr<manage::data::CatalogRepository> catalogRepository;
    std::unique_ptr<manage::data::MySqlBomRepository> bomRepository;
    std::unique_ptr<manage::data::BomService> bomService;
    std::unique_ptr<manage::data::MySqlQuoteLifecycle> quoteLifecycle;
    std::unique_ptr<manage::data::MySqlStatisticsRepository> statisticsRepository;
    std::unique_ptr<manage::data::MaterialBatchService> materialBatchService;
    std::unique_ptr<manage::data::MySqlProcessStepRepository> processStepRepository;
    std::unique_ptr<manage::data::ProcessStepService> processStepService;
    std::unique_ptr<manage::data::MySqlTaskRepository> taskRepository;
    std::unique_ptr<manage::data::TaskService> taskService;
    if (!parser.isSet(smokeTestOption)) {
        // Composition root: every business service below shares this one open
        // database connection. HTTP routes never open ad-hoc connections.
        auto databaseConfig = manage::data::DatabaseConfig::fromEnvironment();
        if (parser.isSet(databaseHostOption)) {
            databaseConfig.host = parser.value(databaseHostOption);
        }
        if (parser.isSet(databasePortOption)) {
            bool databasePortIsValid = false;
            databaseConfig.port = parser.value(databasePortOption)
                                      .toInt(&databasePortIsValid);
            if (!databasePortIsValid) {
                databaseConfig.port = -1;
            }
        }
        if (parser.isSet(databaseNameOption)) {
            databaseConfig.databaseName = parser.value(databaseNameOption);
        }
        if (parser.isSet(databaseUserOption)) {
            databaseConfig.userName = parser.value(databaseUserOption);
        }
        if (parser.isSet(promptDatabasePasswordOption)) {
            // The password remains in this process only and never appears in
            // command history, a settings file, or console output.
            databaseConfig.password = readDatabasePassword();
        }
        databaseConnection = std::make_unique<manage::data::DatabaseConnection>(
            std::move(databaseConfig)
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

        auto userRepository =
            std::make_shared<manage::data::MySqlUserRepository>(
                databaseConnection->database()
            );
        authService = std::make_shared<manage::auth::AuthService>(
            std::static_pointer_cast<manage::auth::UserRepository>(userRepository)
        );
        userManagementService =
            std::make_shared<manage::auth::UserManagementService>(
                std::static_pointer_cast<manage::auth::UserManagementRepository>(
                    userRepository
                )
            );
        catalogRepository = std::make_shared<manage::data::MySqlCatalogRepository>(
            databaseConnection->database()
        );
        bomRepository = std::make_unique<manage::data::MySqlBomRepository>(
            databaseConnection->database()
        );
        bomService = std::make_unique<manage::data::BomService>(*bomRepository);
        quoteLifecycle = std::make_unique<manage::data::MySqlQuoteLifecycle>(
            databaseConnection->database()
        );
        statisticsRepository =
            std::make_unique<manage::data::MySqlStatisticsRepository>(
                databaseConnection->database()
            );
        materialBatchService =
            std::make_unique<manage::data::MaterialBatchService>(
                databaseConnection->database()
            );
        processStepRepository =
            std::make_unique<manage::data::MySqlProcessStepRepository>(
                databaseConnection->database()
            );
        processStepService =
            std::make_unique<manage::data::ProcessStepService>(
                std::move(processStepRepository)
            );
        taskRepository =
            std::make_unique<manage::data::MySqlTaskRepository>(
                databaseConnection->database()
            );
        taskService =
            std::make_unique<manage::data::TaskService>(
                std::move(taskRepository)
            );
    }

    manage::server::ApiServer server(
        std::move(authService),
        std::move(catalogRepository),
        bomService.get(),
        quoteLifecycle.get(),
        std::move(userManagementService),
        statisticsRepository.get(),
        materialBatchService.get(),
        processStepService.get(),
        taskService.get()
    );
    const auto requestedPort = parser.isSet(smokeTestOption)
                                   ? static_cast<quint16>(0)
                                   : static_cast<quint16>(parsedPort);
    const auto listeningPort = server.listen(listenAddress, requestedPort);
    if (listeningPort == 0) {
        qCritical().noquote()
            << QStringLiteral("Unable to listen on %1").arg(listenAddressText);
        return 1;
    }

    qInfo().noquote()
        << QStringLiteral("manage-server listening on http://%1:%2")
               .arg(listenAddressText)
               .arg(listeningPort);

    if (parser.isSet(smokeTestOption)) {
        QTimer::singleShot(50, &application, &QCoreApplication::quit);
    }

    return application.exec();
}
