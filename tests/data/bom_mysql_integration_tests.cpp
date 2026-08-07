#include "manage/data/bom_service.h"
#include "manage/data/database_config.h"
#include "manage/data/database_connection.h"
#include "manage/data/migration_runner.h"
#include "manage/data/mysql_bom_repository.h"

#include <QCoreApplication>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QUuid>

#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const QString& message) {
    if (!condition) {
        throw std::runtime_error(message.toStdString());
    }
}

void removeTestRows(QSqlDatabase database, const QStringList& bomCodes,
                    const QString& materialCode) {
    QSqlQuery query(database);
    for (const auto& bomCode : bomCodes) {
        query.prepare(QStringLiteral("DELETE FROM bom_templates WHERE code = :code"));
        query.bindValue(QStringLiteral(":code"), bomCode);
        query.exec();
    }
    query.prepare(QStringLiteral("DELETE FROM materials WHERE code = :code"));
    query.bindValue(QStringLiteral(":code"), materialCode);
    query.exec();
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    if (qEnvironmentVariable("MANAGE_TEST_MYSQL") != QStringLiteral("1")) {
        std::cout << "[SKIP] set MANAGE_TEST_MYSQL=1 to run the real MySQL BOM test\n";
        return EXIT_SUCCESS;
    }

    const auto config = manage::data::DatabaseConfig::fromEnvironment();
    if (!config.databaseName.endsWith(QStringLiteral("_test"), Qt::CaseInsensitive)) {
        std::cerr << "[REFUSE] MANAGE_DB_NAME must end with _test for the real BOM test\n";
        return EXIT_FAILURE;
    }

    manage::data::DatabaseConnection connection(config);
    QString errorMessage;
    require(connection.open(&errorMessage), errorMessage);
    manage::data::MigrationRunner migrations(connection.database());
    require(migrations.migrate(nullptr, &errorMessage), errorMessage);

    const auto suffix = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto materialCode = QStringLiteral("TEST-MAT-") + suffix;
    const auto copperMaterialCode = QStringLiteral("TEST-CU-") + suffix;
    const auto bomCode = QStringLiteral("TEST-BOM-") + suffix;
    const auto rejectedBomCode = QStringLiteral("TEST-BOM-REJECT-") + suffix;
    qint64 materialId = 0;
    qint64 copperMaterialId = 0;

    try {
        QSqlQuery insertMaterial(connection.database());
        insertMaterial.prepare(QStringLiteral(
            "INSERT INTO materials "
            "(code, name, specification, unit, category, current_unit_price_cents) "
            "VALUES (:code, 'BOM integration material', '', 'piece', 'test', 100)"
        ));
        insertMaterial.bindValue(QStringLiteral(":code"), materialCode);
        require(insertMaterial.exec(), insertMaterial.lastError().text());
        materialId = insertMaterial.lastInsertId().toLongLong();

        manage::data::MySqlBomRepository repository(connection.database());
        manage::data::BomService service(repository);
        manage::data::NewBomTemplate create;
        create.code = bomCode;
        create.name = QStringLiteral("BOM integration template");
        // Deliberately leave description and item notes as null QStrings.
        // The repository must persist them as empty, non-NULL SQL strings.
        create.items = {{10, materialId, 2'000'000, {}}};
        const auto created = service.create(create);
        require(created.ok(), created.message);
        require(created.value->items.size() == 1,
                QStringLiteral("created BOM must contain one item"));
        require(created.value->summary.description.isEmpty(),
                QStringLiteral("omitted description must persist as empty text"));
        require(created.value->items.front().notes.isEmpty(),
                QStringLiteral("omitted item notes must persist as empty text"));

        const manage::data::ReplaceBomItems replacement{
            created.value->summary.id,
            created.value->summary.revision,
            {{20, materialId, 3'000'000, {}}},
        };
        const auto replaced = service.replaceItems(replacement);
        require(replaced.ok(), replaced.message);
        require(replaced.value->summary.revision == 2,
                QStringLiteral("replacement must increase revision"));
        require(replaced.value->items.front().quantityMicros == 3'000'000,
                QStringLiteral("replacement quantity must persist"));
        require(replaced.value->items.front().notes.isEmpty(),
                QStringLiteral("omitted replacement notes must persist as empty text"));

        const manage::data::SetBomEnabled disable{
            replaced.value->summary.id,
            false,
            replaced.value->summary.revision,
        };
        const auto disabled = service.setEnabled(disable);
        require(disabled.ok() && !disabled.value->summary.isEnabled,
                QStringLiteral("BOM must be disabled"));

        QSqlQuery disableMaterial(connection.database());
        disableMaterial.prepare(QStringLiteral(
            "UPDATE materials SET is_enabled = FALSE WHERE id = :id"
        ));
        disableMaterial.bindValue(QStringLiteral(":id"), materialId);
        require(disableMaterial.exec(), disableMaterial.lastError().text());

        manage::data::NewBomTemplate invalidCreate;
        invalidCreate.code = rejectedBomCode;
        invalidCreate.name = QStringLiteral("Must not be saved");
        invalidCreate.items = {{10, materialId, 1'000'000, {}}};
        manage::data::BomTemplate ignored;
        QString repositoryError;
        const auto repositoryStatus = repository.create(
            invalidCreate,
            ignored,
            repositoryError
        );
        require(
            repositoryStatus == manage::data::BomRepositoryStatus::InvalidMaterial,
            QStringLiteral("repository must reject a disabled material inside its transaction")
        );
        require(repositoryError.contains(QStringLiteral("disabled")),
                QStringLiteral("repository must explain the disabled material"));

        // 批次9：电线类物料 + 供应商 + 铜价档，构建 BOM 时解析真实单价。
        QSqlQuery insertCopperMaterial(connection.database());
        insertCopperMaterial.prepare(QStringLiteral(
            "INSERT INTO materials (code, name, specification, unit, category, "
            "current_unit_price_cents, is_copper_based) "
            "VALUES (:code, 'Copper wire', '2.5mm', 'meter', 'wire', 5000, TRUE)"
        ));
        insertCopperMaterial.bindValue(QStringLiteral(":code"), copperMaterialCode);
        require(insertCopperMaterial.exec(), insertCopperMaterial.lastError().text());
        copperMaterialId = insertCopperMaterial.lastInsertId().toLongLong();

        QSqlQuery insertSupplier(connection.database());
        insertSupplier.prepare(QStringLiteral(
            "INSERT INTO material_suppliers (material_id, supplier_name, is_default) "
            "VALUES (:materialId, '铜业一厂', TRUE)"
        ));
        insertSupplier.bindValue(QStringLiteral(":materialId"), copperMaterialId);
        require(insertSupplier.exec(), insertSupplier.lastError().text());
        const auto supplierId = insertSupplier.lastInsertId().toLongLong();

        QSqlQuery insertPrice(connection.database());
        insertPrice.prepare(QStringLiteral(
            "INSERT INTO material_supplier_prices "
            "(material_supplier_id, copper_price_cents, unit_price_cents) "
            "VALUES (:supplierId, :copper, :unit)"
        ));
        insertPrice.bindValue(QStringLiteral(":supplierId"), supplierId);
        insertPrice.bindValue(QStringLiteral(":copper"), 6'000'000);
        insertPrice.bindValue(QStringLiteral(":unit"), 8'000);
        require(insertPrice.exec(), insertPrice.lastError().text());
        insertPrice.bindValue(QStringLiteral(":copper"), 7'000'000);
        insertPrice.bindValue(QStringLiteral(":unit"), 9'000);
        require(insertPrice.exec(), insertPrice.lastError().text());

        const auto copperBomCode = QStringLiteral("TEST-BOM-CU-") + suffix;
        manage::data::NewBomTemplate copperCreate;
        copperCreate.code = copperBomCode;
        copperCreate.name = QStringLiteral("Copper BOM");
        // 输入当前铜价 75000 元/吨：向下取档到 70000 档，单价 9000 分。
        copperCreate.items = {{
            10,
            copperMaterialId,
            1'000'000,
            {},
            supplierId,
            std::optional<qint64>(7'500'000),
        }};
        const auto copperCreated = service.create(copperCreate);
        require(copperCreated.ok(), copperCreated.message);
        const auto& copperItem = copperCreated.value->items.front();
        require(copperItem.materialSupplierId == supplierId,
                QStringLiteral("BOM item must persist supplier id"));
        require(copperItem.supplierName == QStringLiteral("铜业一厂"),
                QStringLiteral("BOM item must persist supplier name snapshot"));
        require(copperItem.unitPriceCents == 9'000,
                QStringLiteral("BOM item must resolve real unit price from copper tier"));
        require(copperItem.copperPriceCents.has_value() &&
                    *copperItem.copperPriceCents == 7'000'000,
                QStringLiteral("BOM item must persist matched copper tier"));

        // 有供应商的电线类物料：未选供应商 / 未填铜价均被拒绝。
        manage::data::NewBomTemplate missingSupplier;
        missingSupplier.code = QStringLiteral("TEST-BOM-NOSUP-") + suffix;
        missingSupplier.name = QStringLiteral("Missing supplier");
        missingSupplier.items = {{10, copperMaterialId, 1'000'000, {}}};
        const auto noSupplier = service.create(missingSupplier);
        require(!noSupplier.ok() &&
                    noSupplier.error == manage::data::BomErrorCode::Validation,
                QStringLiteral("copper material with suppliers must require a supplier"));

        manage::data::NewBomTemplate missingCopper;
        missingCopper.code = QStringLiteral("TEST-BOM-NOCU-") + suffix;
        missingCopper.name = QStringLiteral("Missing copper tier");
        missingCopper.items = {{
            10,
            copperMaterialId,
            1'000'000,
            {},
            supplierId,
            std::nullopt,
        }};
        const auto noCopper = service.create(missingCopper);
        require(!noCopper.ok() &&
                    noCopper.error == manage::data::BomErrorCode::Validation,
                QStringLiteral("copper material with supplier must require a copper tier"));

        removeTestRows(
            connection.database(),
            QStringList{
                bomCode,
                rejectedBomCode,
                copperBomCode,
                QStringLiteral("TEST-BOM-NOSUP-") + suffix,
                QStringLiteral("TEST-BOM-NOCU-") + suffix,
            },
            materialCode
        );
        removeTestRows(
            connection.database(),
            QStringList{},
            copperMaterialCode
        );
        std::cout << "[PASS] real MySQL BOM create/replace/disable flow\n";
        std::cout << "[PASS] real MySQL BOM supplier copper-tier pricing\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        removeTestRows(
            connection.database(),
            QStringList{
                bomCode,
                rejectedBomCode,
                QStringLiteral("TEST-BOM-CU-") + suffix,
                QStringLiteral("TEST-BOM-NOSUP-") + suffix,
                QStringLiteral("TEST-BOM-NOCU-") + suffix,
            },
            materialCode
        );
        removeTestRows(
            connection.database(),
            QStringList{},
            copperMaterialCode
        );
        std::cerr << "[FAIL] " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
