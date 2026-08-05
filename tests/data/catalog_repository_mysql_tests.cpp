#include "manage/data/catalog_repository.h"
#include "manage/data/database_config.h"
#include "manage/data/database_connection.h"
#include "manage/data/migration_runner.h"

#include <QCoreApplication>
#include <QSqlDatabase>
#include <QUuid>
#include <QtGlobal>

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

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

void requireRepository(
    bool success,
    const manage::data::RepositoryError& error,
    const std::string& operation
) {
    if (!success) {
        throw TestFailure(operation + ": " + error.message.toStdString());
    }
}

class RollbackGuard final {
public:
    explicit RollbackGuard(QSqlDatabase database) : database_(std::move(database)) {}
    ~RollbackGuard() {
        if (active_) {
            database_.rollback();
        }
    }

    bool rollback() {
        if (!active_) {
            return true;
        }
        active_ = false;
        return database_.rollback();
    }

    RollbackGuard(const RollbackGuard&) = delete;
    RollbackGuard& operator=(const RollbackGuard&) = delete;

private:
    QSqlDatabase database_;
    bool active_{true};
};

bool runMySqlRepositoryIntegration() {
    if (qEnvironmentVariable("MANAGE_TEST_MYSQL") != QStringLiteral("1")) {
        std::cout << "[SKIP] MySQL catalog repository integration "
                     "(set MANAGE_TEST_MYSQL=1)\n";
        return false;
    }

    const auto config = manage::data::DatabaseConfig::fromEnvironment();
    require(
        config.databaseName.endsWith(QStringLiteral("_test")),
        "integration database name must end with _test"
    );

    manage::data::DatabaseConnection connection(config);
    QString migrationError;
    require(connection.open(&migrationError), migrationError.toStdString());

    manage::data::MigrationRunner migrationRunner(connection.database());
    manage::data::MigrationReport migrationReport;
    require(
        migrationRunner.migrate(&migrationReport, &migrationError),
        migrationError.toStdString()
    );
    require(migrationReport.currentVersion == 2, "catalog test schema version");

    auto database = connection.database();
    require(database.transaction(), "unable to start catalog integration transaction");
    RollbackGuard rollback(database);
    manage::data::MySqlCatalogRepository repository(database);
    manage::data::RepositoryError repositoryError;

    const auto suffix = QUuid::createUuid().toString(QUuid::WithoutBraces);
    manage::data::MaterialDraft materialDraft;
    materialDraft.code = QStringLiteral("IT-%1").arg(suffix);
    materialDraft.name = QStringLiteral("Integration material");
    materialDraft.unit = QStringLiteral("piece");
    materialDraft.currentUnitPriceCents = 12'345;
    require(materialDraft.specification.isNull(), "test specification starts null");
    require(materialDraft.category.isNull(), "test category starts null");

    manage::data::Material createdMaterial;
    requireRepository(
        repository.createMaterial(materialDraft, &createdMaterial, &repositoryError),
        repositoryError,
        "create material"
    );
    require(createdMaterial.id > 0, "created material id");
    require(createdMaterial.revision == 1, "created material revision");

    manage::data::Material readMaterial;
    requireRepository(
        repository.findMaterial(createdMaterial.id, &readMaterial, &repositoryError),
        repositoryError,
        "read material"
    );
    require(readMaterial.code == materialDraft.code, "material read after create");
    require(
        readMaterial.specification.isEmpty() && !readMaterial.specification.isNull(),
        "null material specification is stored as an empty string"
    );
    require(
        readMaterial.category.isEmpty() && !readMaterial.category.isNull(),
        "null material category is stored as an empty string"
    );

    auto changedMaterial = materialDraft;
    changedMaterial.name = QStringLiteral("Updated integration material");
    manage::data::Material updatedMaterial;
    requireRepository(
        repository.updateMaterial(
            createdMaterial.id,
            1,
            changedMaterial,
            &updatedMaterial,
            &repositoryError
        ),
        repositoryError,
        "update material"
    );
    require(updatedMaterial.revision == 2, "material update increments revision");

    manage::data::Material staleMaterial;
    const auto staleMaterialUpdated = repository.updateMaterial(
        createdMaterial.id,
        1,
        changedMaterial,
        &staleMaterial,
        &repositoryError
    );
    require(!staleMaterialUpdated, "stale material update must fail");
    require(
        repositoryError.code == manage::data::RepositoryErrorCode::RevisionConflict,
        "stale material update error code"
    );

    manage::data::Material disabledMaterial;
    requireRepository(
        repository.setMaterialEnabled(
            createdMaterial.id,
            2,
            false,
            &disabledMaterial,
            &repositoryError
        ),
        repositoryError,
        "disable material"
    );
    require(!disabledMaterial.isEnabled, "material is disabled, not deleted");
    require(disabledMaterial.revision == 3, "disable increments material revision");

    manage::data::CustomerDraft customerDraft;
    customerDraft.name = QStringLiteral("Integration customer %1").arg(suffix);
    require(customerDraft.contactName.isNull(), "test contact starts null");
    require(customerDraft.phone.isNull(), "test phone starts null");
    require(customerDraft.address.isNull(), "test address starts null");
    require(customerDraft.notes.isNull(), "test notes start null");
    manage::data::Customer createdCustomer;
    requireRepository(
        repository.createCustomer(customerDraft, &createdCustomer, &repositoryError),
        repositoryError,
        "create customer"
    );
    require(createdCustomer.id > 0, "created customer id");

    manage::data::Customer readCustomer;
    requireRepository(
        repository.findCustomer(createdCustomer.id, &readCustomer, &repositoryError),
        repositoryError,
        "read customer"
    );
    require(readCustomer.name == customerDraft.name, "customer read after create");
    require(
        readCustomer.contactName.isEmpty() && !readCustomer.contactName.isNull(),
        "null customer contact is stored as an empty string"
    );
    require(
        readCustomer.phone.isEmpty() && !readCustomer.phone.isNull(),
        "null customer phone is stored as an empty string"
    );
    require(
        readCustomer.address.isEmpty() && !readCustomer.address.isNull(),
        "null customer address is stored as an empty string"
    );
    require(
        readCustomer.notes.isEmpty() && !readCustomer.notes.isNull(),
        "null customer notes are stored as an empty string"
    );

    auto changedCustomer = customerDraft;
    changedCustomer.phone = QStringLiteral("654321");
    manage::data::Customer updatedCustomer;
    requireRepository(
        repository.updateCustomer(
            createdCustomer.id,
            1,
            changedCustomer,
            &updatedCustomer,
            &repositoryError
        ),
        repositoryError,
        "update customer"
    );
    require(updatedCustomer.revision == 2, "customer update increments revision");

    manage::data::Customer staleCustomer;
    const auto staleCustomerUpdated = repository.updateCustomer(
        createdCustomer.id,
        1,
        changedCustomer,
        &staleCustomer,
        &repositoryError
    );
    require(!staleCustomerUpdated, "stale customer update must fail");
    require(
        repositoryError.code == manage::data::RepositoryErrorCode::RevisionConflict,
        "stale customer update error code"
    );

    require(rollback.rollback(), "unable to roll back catalog integration data");
    require(
        !repository.findMaterial(createdMaterial.id, &readMaterial, &repositoryError) &&
            repositoryError.code == manage::data::RepositoryErrorCode::NotFound,
        "material test row must not remain after rollback"
    );
    require(
        !repository.findCustomer(createdCustomer.id, &readCustomer, &repositoryError) &&
            repositoryError.code == manage::data::RepositoryErrorCode::NotFound,
        "customer test row must not remain after rollback"
    );
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    try {
        if (!runMySqlRepositoryIntegration()) {
            return EXIT_SUCCESS;
        }
        std::cout << "[PASS] catalog repository MySQL safety and lifecycle\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] catalog repository MySQL: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
