#include "manage/data/database_config.h"
#include "manage/data/database_connection.h"
#include "manage/data/migration_runner.h"
#include "manage/data/mysql_user_repository.h"

#include <QCoreApplication>
#include <QSqlQuery>
#include <QtGlobal>

#include <cstdlib>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

class TestFailure final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw TestFailure(message);
    }
}

void builtInCatalogContainsThePublishedSchemas() {
    QString error;
    const auto migrations = manage::data::MigrationRunner::builtInMigrations(
        &error
    );

    require(error.isEmpty(), error.toStdString());
    require(migrations.size() == 7, "seven built-in migrations expected");
    const auto& migration = migrations.at(0);
    require(migration.version == 1, "initial migration version");
    require(
        migration.name == QStringLiteral("001_initial_schema"),
        "initial migration name"
    );
    require(migration.checksum.size() == 64, "SHA-256 checksum length");

    const std::vector<QString> requiredTables = {
        QStringLiteral("roles"),
        QStringLiteral("users"),
        QStringLiteral("materials"),
        QStringLiteral("customers"),
        QStringLiteral("bom_templates"),
        QStringLiteral("bom_items"),
        QStringLiteral("quotes"),
        QStringLiteral("quote_items"),
    };
    for (const auto& table : requiredTables) {
        require(
            migration.sql.contains(
                QStringLiteral("CREATE TABLE %1").arg(table),
                Qt::CaseInsensitive
            ),
            QStringLiteral("missing table %1").arg(table).toStdString()
        );
    }
    require(
        migration.sql.contains(QStringLiteral("'admin'")),
        "bootstrap admin record"
    );

    const auto& quoteMigration = migrations.at(1);
    require(quoteMigration.version == 2, "quote BOM migration version");
    require(
        quoteMigration.name == QStringLiteral("002_quote_bom_template"),
        "quote BOM migration name"
    );
    require(quoteMigration.checksum.size() == 64, "quote migration checksum");
    require(
        quoteMigration.sql.contains(QStringLiteral("ADD COLUMN bom_template_id")),
        "quote migration adds optional BOM reference"
    );
    require(
        quoteMigration.sql.contains(QStringLiteral("ix_quotes_bom_template")),
        "quote migration adds BOM lookup index"
    );
    require(
        quoteMigration.sql.contains(QStringLiteral("fk_quotes_bom_template")),
        "quote migration adds BOM foreign key"
    );

    const auto& quantityMigration = migrations.at(2);
    require(quantityMigration.version == 3, "quote BOM quantity migration version");
    require(
        quantityMigration.name == QStringLiteral("003_quote_bom_quantity"),
        "quote BOM quantity migration name"
    );
    require(
        quantityMigration.sql.contains(QStringLiteral("bom_quantity_micros")),
        "quote migration adds BOM quantity"
    );
}

void splitterPreservesQuotedSemicolonsAndRemovesComments() {
    const auto sql = QStringLiteral(R"SQL(
        -- SQL comments do not become statements
        CREATE TABLE `semi;colon` (`value` VARCHAR(20));
        INSERT INTO `semi;colon` VALUES ('a;b'), ("c;d"); # comment ;
        /* block comment with a ; */ SELECT 'it''s;fine';
    )SQL");

    QString error;
    const auto statements =
        manage::data::MigrationRunner::splitSqlStatements(sql, &error);
    require(error.isEmpty(), error.toStdString());
    require(statements.size() == 3, "three SQL statements expected");
    require(
        statements.at(0).contains(QStringLiteral("`semi;colon`")),
        "backtick-quoted semicolon must be preserved"
    );
    require(
        statements.at(1).contains(QStringLiteral("'a;b'")),
        "single-quoted semicolon must be preserved"
    );
    require(
        statements.at(2).contains(QStringLiteral("'it''s;fine'")),
        "escaped quote and semicolon must be preserved"
    );
}

void splitterRejectsUnterminatedInput() {
    QString error;
    const auto statements = manage::data::MigrationRunner::splitSqlStatements(
        QStringLiteral("SELECT 1; /* unfinished"),
        &error
    );
    require(statements.isEmpty(), "invalid SQL must not return statements");
    require(!error.isEmpty(), "invalid SQL must report an error");
}

void optionalMySqlIntegrationTest() {
    if (!qEnvironmentVariableIsSet("MANAGE_TEST_MYSQL") ||
        qEnvironmentVariable("MANAGE_TEST_MYSQL") != QStringLiteral("1")) {
        std::cout << "[SKIP] MySQL integration (set MANAGE_TEST_MYSQL=1)\n";
        return;
    }

    const auto config = manage::data::DatabaseConfig::fromEnvironment();
    require(
        config.databaseName.endsWith(QStringLiteral("_test")),
        "integration database name must end with _test"
    );

    manage::data::DatabaseConnection connection(config);
    QString error;
    require(connection.open(&error), error.toStdString());

    manage::data::MigrationRunner runner(connection.database());
    manage::data::MigrationReport firstReport;
    require(runner.migrate(&firstReport, &error), error.toStdString());
    require(firstReport.currentVersion == 8, "database schema version after migrate");

    manage::data::MigrationReport secondReport;
    require(runner.migrate(&secondReport, &error), error.toStdString());
    require(secondReport.currentVersion == 8, "schema version after repeat migrate");
    require(secondReport.appliedCount == 0, "repeat migration must be idempotent");

    QSqlQuery query(connection.database());
    require(query.exec(QStringLiteral("SELECT COUNT(*) FROM roles")), "query roles");
    require(query.next() && query.value(0).toInt() == 3, "three roles expected");
    require(
        query.exec(QStringLiteral(
            "SELECT is_enabled, must_change_password FROM users "
            "WHERE username = 'admin'"
        )),
        "query bootstrap admin"
    );
    require(query.next(), "bootstrap admin expected");
    require(!query.value(0).toBool(), "bootstrap admin starts disabled");
    require(query.value(1).toBool(), "bootstrap admin must change password");

    manage::data::MySqlUserRepository users(connection.database());
    manage::auth::UserAccount admin;
    require(
        users.findByUsername(QStringLiteral("admin"), &admin, &error) ==
            manage::auth::RepositoryResult::Success,
        error.toStdString()
    );
    require(admin.role == manage::auth::UserRole::Admin, "admin role repository mapping");
    require(!admin.enabled, "repository reads initial admin as disabled");
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);

    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"built-in catalog", builtInCatalogContainsThePublishedSchemas},
        {"SQL splitter", splitterPreservesQuotedSemicolonsAndRemovesComments},
        {"invalid SQL", splitterRejectsUnterminatedInput},
        {"optional MySQL integration", optionalMySqlIntegrationTest},
    };

    std::size_t passed = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            ++passed;
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
        }
    }

    std::cout << passed << "/" << tests.size() << " tests passed\n";
    return passed == tests.size() ? EXIT_SUCCESS : EXIT_FAILURE;
}
