#include "manage/data/database_config.h"
#include "manage/data/database_connection.h"
#include "manage/data/migration_runner.h"
#include "manage/data/mysql_quote_lifecycle.h"

#include <QCoreApplication>
#include <QSqlError>
#include <QSqlQuery>
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

void requireSql(QSqlQuery& query, const QString& operation) {
    require(query.exec(), operation + QStringLiteral(": ") + query.lastError().text());
}

void cleanup(
    QSqlDatabase database,
    qint64 customerId,
    qint64 bomId,
    qint64 materialId
) {
    QSqlQuery query(database);
    if (customerId > 0) {
        query.prepare(QStringLiteral("DELETE FROM quotes WHERE customer_id = :id"));
        query.bindValue(QStringLiteral(":id"), customerId);
        query.exec();
    }
    if (bomId > 0) {
        query.prepare(QStringLiteral("DELETE FROM bom_templates WHERE id = :id"));
        query.bindValue(QStringLiteral(":id"), bomId);
        query.exec();
    }
    if (materialId > 0) {
        query.prepare(QStringLiteral("DELETE FROM materials WHERE id = :id"));
        query.bindValue(QStringLiteral(":id"), materialId);
        query.exec();
    }
    if (customerId > 0) {
        query.prepare(QStringLiteral("DELETE FROM customers WHERE id = :id"));
        query.bindValue(QStringLiteral(":id"), customerId);
        query.exec();
    }
}

qint64 quoteCountForCustomer(QSqlDatabase database, qint64 customerId) {
    QSqlQuery query(database);
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM quotes WHERE customer_id = :id"));
    query.bindValue(QStringLiteral(":id"), customerId);
    requireSql(query, QStringLiteral("count customer quotes"));
    require(query.next(), QStringLiteral("quote count row missing"));
    return query.value(0).toLongLong();
}

manage::data::QuoteDraft draftFor(
    qint64 customerId,
    qint64 bomId,
    qint64 materialId,
    qint64 supplierId = 0
) {
    manage::data::QuoteDraft draft;
    draft.customerId = customerId;
    draft.bomTemplateId = bomId;
    draft.bomQuantityMicros = 3'000'000;
    draft.freightCents = 100;
    draft.otherFeesCents = 50;
    draft.markupBasisPoints = 1000;
    draft.taxBasisPoints = 1300;
    draft.notes = QStringLiteral("integration draft");
    draft.items = {{
        materialId,
        2'500'000,
        1234,
        QStringLiteral("snapshot line"),
        // 电线类物料铜价档（元/吨，精确到分）。
        std::optional<qint64>(7'000'000),
        // 批次9：报价行选定的供应商（0 = 未指定）。
        supplierId,
    }};
    return draft;
}

// 批次9：报价行指定供应商时校验归属并快照供应商名称。
void runSupplierSnapshot(QSqlDatabase database) {
    const auto suffix = QUuid::createUuid().toString(QUuid::WithoutBraces);
    qint64 customerId = 0;
    qint64 materialId = 0;
    qint64 bomId = 0;
    qint64 supplierId = 0;

    try {
        QSqlQuery customer(database);
        customer.prepare(QStringLiteral(
            "INSERT INTO customers (name) VALUES (:name)"
        ));
        customer.bindValue(QStringLiteral(":name"), QStringLiteral("Supplier Quote ") + suffix);
        requireSql(customer, QStringLiteral("insert supplier quote customer"));
        customerId = customer.lastInsertId().toLongLong();

        QSqlQuery material(database);
        material.prepare(QStringLiteral(
            "INSERT INTO materials (code, name, specification, unit, category, "
            "current_unit_price_cents, is_copper_based) VALUES (:code, "
            "'Copper Wire', '2.5mm', 'meter', 'wire', 5000, TRUE)"
        ));
        material.bindValue(QStringLiteral(":code"), QStringLiteral("SUP-MAT-") + suffix);
        requireSql(material, QStringLiteral("insert supplier quote material"));
        materialId = material.lastInsertId().toLongLong();

        QSqlQuery supplier(database);
        supplier.prepare(QStringLiteral(
            "INSERT INTO material_suppliers "
            "(material_id, supplier_name, contact_name, phone) "
            "VALUES (:materialId, '铜业一厂', 'Contact', '123456')"
        ));
        supplier.bindValue(QStringLiteral(":materialId"), materialId);
        requireSql(supplier, QStringLiteral("insert quote material supplier"));
        supplierId = supplier.lastInsertId().toLongLong();

        QSqlQuery bom(database);
        bom.prepare(QStringLiteral(
            "INSERT INTO bom_templates (code, name, description) "
            "VALUES (:code, 'Supplier BOM', '')"
        ));
        bom.bindValue(QStringLiteral(":code"), QStringLiteral("SUP-BOM-") + suffix);
        requireSql(bom, QStringLiteral("insert supplier quote BOM"));
        bomId = bom.lastInsertId().toLongLong();

        manage::data::MySqlQuoteLifecycle lifecycle(database);
        const auto created = lifecycle.create({
            draftFor(customerId, bomId, materialId, supplierId),
            1,
        });
        require(created.ok(), QStringLiteral("create supplier snapshot quote: %1").arg(created.message));
        require(created.value->items.size() == 1, QStringLiteral("supplier quote item count"));
        const auto& item = created.value->items.front();
        require(item.materialSupplierId == supplierId,
                QStringLiteral("quote item must snapshot materialSupplierId"));
        require(item.supplierName == QStringLiteral("铜业一厂"),
                QStringLiteral("quote item must snapshot supplier name"));

        // 克隆保留供应商快照。
        const auto cloned = lifecycle.clone({
            created.value->summary.id,
            1,
        });
        require(cloned.ok(), QStringLiteral("clone supplier snapshot quote"));
        require(cloned.value->items.front().materialSupplierId == supplierId &&
                    cloned.value->items.front().supplierName == QStringLiteral("铜业一厂"),
                QStringLiteral("cloned quote keeps supplier snapshot"));

        // 不属于该物料的供应商被拒绝。
        QSqlQuery otherMaterial(database);
        otherMaterial.prepare(QStringLiteral(
            "INSERT INTO materials (code, name, specification, unit, category, "
            "current_unit_price_cents) VALUES (:code, 'Other', '', 'piece', 'test', 100)"
        ));
        otherMaterial.bindValue(QStringLiteral(":code"), QStringLiteral("SUP-OTHER-") + suffix);
        requireSql(otherMaterial, QStringLiteral("insert other material"));
        const auto otherMaterialId = otherMaterial.lastInsertId().toLongLong();
        const auto rejected = lifecycle.create({
            draftFor(customerId, bomId, otherMaterialId, supplierId),
            1,
        });
        require(!rejected.ok() &&
                    rejected.error == manage::data::QuoteErrorCode::Validation,
                QStringLiteral("supplier not belonging to material must be rejected"));
    } catch (...) {
        cleanup(database, customerId, bomId, materialId);
        throw;
    }
    cleanup(database, customerId, bomId, materialId);
}

void runLifecycle(QSqlDatabase database) {
    const auto suffix = QUuid::createUuid().toString(QUuid::WithoutBraces);
    qint64 customerId = 0;
    qint64 materialId = 0;
    qint64 bomId = 0;

    try {
        QSqlQuery customer(database);
        customer.prepare(QStringLiteral(
            "INSERT INTO customers (name, contact_name, phone, address, notes) "
            "VALUES (:name, 'Original Contact', '10086', 'Original Address', '')"
        ));
        customer.bindValue(QStringLiteral(":name"), QStringLiteral("Quote Customer ") + suffix);
        requireSql(customer, QStringLiteral("insert quote customer"));
        customerId = customer.lastInsertId().toLongLong();

        QSqlQuery material(database);
        material.prepare(QStringLiteral(
            "INSERT INTO materials (code, name, specification, unit, category, "
            "current_unit_price_cents) VALUES (:code, 'Original Material', "
            "'Original Spec', 'piece', 'test', 1234)"
        ));
        material.bindValue(QStringLiteral(":code"), QStringLiteral("QUOTE-MAT-") + suffix);
        requireSql(material, QStringLiteral("insert quote material"));
        materialId = material.lastInsertId().toLongLong();

        QSqlQuery bom(database);
        bom.prepare(QStringLiteral(
            "INSERT INTO bom_templates (code, name, description) "
            "VALUES (:code, 'Quote BOM', '')"
        ));
        bom.bindValue(QStringLiteral(":code"), QStringLiteral("QUOTE-BOM-") + suffix);
        requireSql(bom, QStringLiteral("insert quote BOM"));
        bomId = bom.lastInsertId().toLongLong();

        QSqlQuery bomItem(database);
        bomItem.prepare(QStringLiteral(
            "INSERT INTO bom_items (bom_template_id, line_no, material_id, "
            "quantity_micros, notes) VALUES (:bom, 1, :material, 2500000, '')"
        ));
        bomItem.bindValue(QStringLiteral(":bom"), bomId);
        bomItem.bindValue(QStringLiteral(":material"), materialId);
        requireSql(bomItem, QStringLiteral("insert quote BOM item"));

        manage::data::MySqlQuoteLifecycle lifecycle(database);
        auto createDraft = draftFor(customerId, bomId, materialId);
        const auto created = lifecycle.create({createDraft, 1});
        require(created.ok(), created.message);
        require(created.value->summary.status == manage::data::QuoteStatus::Draft,
                QStringLiteral("created quote must be draft"));
        require(created.value->summary.quoteNumber.startsWith(QStringLiteral("Q-")),
                QStringLiteral("server quote number"));
        require(created.value->summary.bomTemplateId == std::optional<qint64>(bomId),
                QStringLiteral("BOM relationship"));
        require(created.value->summary.bomQuantityMicros == 3'000'000,
                QStringLiteral("BOM sales quantity"));
        require(created.value->materialCostCents == 3085,
                QStringLiteral("fixed-point line calculation"));
        require(created.value->markupAmountCents == 324,
                QStringLiteral("fixed-point markup calculation"));
        require(created.value->priceBeforeTaxCents == 3559,
                QStringLiteral("fixed-point before-tax amount"));
        require(created.value->taxAmountCents == 463,
                QStringLiteral("fixed-point tax calculation"));
        require(created.value->priceWithTaxCents == 4022,
                QStringLiteral("fixed-point total calculation"));
        require(created.value->items.size() == 1,
                QStringLiteral("created quote item count"));
        require(created.value->items.front().materialName == QStringLiteral("Original Material"),
                QStringLiteral("material name snapshot"));
        require(created.value->items.front().copperPriceCents ==
                    std::optional<qint64>(7'000'000),
                QStringLiteral("copper tier snapshot saved with quote"));
        require(created.value->customerContact == QStringLiteral("Original Contact"),
                QStringLiteral("customer contact snapshot"));

        const auto beforeFailedCreate = quoteCountForCustomer(database, customerId);
        auto lateFailureDraft = createDraft;
        lateFailureDraft.items.push_back({
            materialId,
            1'000'000,
            100,
            QString(600, QLatin1Char('x')),
        });
        const auto lateFailure = lifecycle.create({lateFailureDraft, 1});
        require(!lateFailure.ok() &&
                    lateFailure.error == manage::data::QuoteErrorCode::Validation,
                QStringLiteral("late item failure must map to validation"));
        require(quoteCountForCustomer(database, customerId) == beforeFailedCreate,
                QStringLiteral("failed header/items transaction must roll back"));

        QSqlQuery changeSources(database);
        changeSources.prepare(QStringLiteral(
            "UPDATE customers SET name = 'Updated Customer', "
            "contact_name = 'Updated Contact' WHERE id = :id"
        ));
        changeSources.bindValue(QStringLiteral(":id"), customerId);
        requireSql(changeSources, QStringLiteral("update source customer"));
        changeSources.prepare(QStringLiteral(
            "UPDATE materials SET name = 'Updated Material', "
            "specification = 'Updated Spec' WHERE id = :id"
        ));
        changeSources.bindValue(QStringLiteral(":id"), materialId);
        requireSql(changeSources, QStringLiteral("update source material"));

        const auto stillSnapshotted = lifecycle.getById(created.value->summary.id);
        require(stillSnapshotted.ok(), stillSnapshotted.message);
        require(stillSnapshotted.value->summary.customerName != QStringLiteral("Updated Customer"),
                QStringLiteral("saved customer snapshot must not drift"));
        require(stillSnapshotted.value->items.front().materialName == QStringLiteral("Original Material"),
                QStringLiteral("saved material snapshot must not drift"));

        auto updatedDraft = createDraft;
        updatedDraft.freightCents = 200;
        updatedDraft.notes = QStringLiteral("updated draft");
        const auto updated = lifecycle.update({
            created.value->summary.id,
            created.value->summary.revision,
            updatedDraft,
            1,
        });
        require(updated.ok(), updated.message);
        require(updated.value->summary.revision == 2,
                QStringLiteral("draft update must increment revision"));
        require(updated.value->summary.customerName == QStringLiteral("Updated Customer"),
                QStringLiteral("draft update refreshes customer snapshot"));
        require(updated.value->items.front().materialName == QStringLiteral("Updated Material"),
                QStringLiteral("draft update refreshes material snapshot"));

        auto staleDraft = updatedDraft;
        staleDraft.notes = QStringLiteral("must not persist");
        const auto staleUpdate = lifecycle.update({
            created.value->summary.id,
            1,
            staleDraft,
            1,
        });
        require(!staleUpdate.ok() && staleUpdate.error == manage::data::QuoteErrorCode::Conflict,
                QStringLiteral("stale update must conflict"));
        const auto afterConflict = lifecycle.getById(created.value->summary.id);
        require(afterConflict.ok() && afterConflict.value->notes == QStringLiteral("updated draft"),
                QStringLiteral("conflict must not change saved quote"));

        const auto listed = lifecycle.list({
            1,
            20,
            QStringLiteral("Updated Customer"),
            manage::data::QuoteStatus::Draft,
            customerId,
        });
        require(listed.ok() && listed.value->total == 1 && listed.value->items.size() == 1,
                QStringLiteral("quote list filters"));

        const auto issued = lifecycle.changeStatus({
            created.value->summary.id,
            updated.value->summary.revision,
            manage::data::QuoteStatus::Issued,
            1,
        });
        require(issued.ok() && issued.value->summary.status == manage::data::QuoteStatus::Issued,
                issued.message);
        require(issued.value->issuedAt.isValid(), QStringLiteral("issued timestamp"));
        const auto frozenUpdate = lifecycle.update({
            issued.value->summary.id,
            issued.value->summary.revision,
            updatedDraft,
            1,
        });
        require(!frozenUpdate.ok() && frozenUpdate.error == manage::data::QuoteErrorCode::Conflict,
                QStringLiteral("issued quote content must be frozen"));

        const auto cloned = lifecycle.clone({issued.value->summary.id, 1});
        require(cloned.ok(), cloned.message);
        require(cloned.value->summary.status == manage::data::QuoteStatus::Draft,
                QStringLiteral("clone must be a draft"));
        require(cloned.value->sourceQuoteId == std::optional<qint64>(issued.value->summary.id),
                QStringLiteral("clone source relationship"));
        require(cloned.value->summary.quoteNumber != issued.value->summary.quoteNumber,
                QStringLiteral("clone receives a new quote number"));
        require(cloned.value->items.front().materialName == issued.value->items.front().materialName,
                QStringLiteral("clone preserves frozen snapshots"));
        require(cloned.value->items.front().copperPriceCents ==
                    issued.value->items.front().copperPriceCents,
                QStringLiteral("clone preserves copper tier snapshot"));

        const auto staleDelete = lifecycle.deleteDraft({
            cloned.value->summary.id,
            cloned.value->summary.revision + 1,
            1,
        });
        require(!staleDelete.ok() && staleDelete.error == manage::data::QuoteErrorCode::Conflict,
                QStringLiteral("stale draft delete must conflict"));
        const auto deletedClone = lifecycle.deleteDraft({
            cloned.value->summary.id,
            cloned.value->summary.revision,
            1,
        });
        require(deletedClone.ok(), deletedClone.message);
        const auto missingClone = lifecycle.getById(cloned.value->summary.id);
        require(!missingClone.ok() && missingClone.error == manage::data::QuoteErrorCode::NotFound,
                QStringLiteral("deleted draft must disappear"));

        const auto voided = lifecycle.changeStatus({
            issued.value->summary.id,
            issued.value->summary.revision,
            manage::data::QuoteStatus::Void,
            1,
        });
        require(voided.ok() && voided.value->summary.status == manage::data::QuoteStatus::Void,
                voided.message);
        require(voided.value->issuedAt.isValid() && voided.value->voidedAt.isValid(),
                QStringLiteral("void keeps issue time and records void time"));
        const auto illegalTransition = lifecycle.changeStatus({
            voided.value->summary.id,
            voided.value->summary.revision,
            manage::data::QuoteStatus::Issued,
            1,
        });
        require(!illegalTransition.ok() &&
                    illegalTransition.error == manage::data::QuoteErrorCode::InvalidTransition,
                QStringLiteral("void quote cannot be issued again"));
        const auto deleteVoid = lifecycle.deleteDraft({
            voided.value->summary.id,
            voided.value->summary.revision,
            1,
        });
        require(!deleteVoid.ok() && deleteVoid.error == manage::data::QuoteErrorCode::Conflict,
                QStringLiteral("void quote cannot be deleted"));

        const auto cloneVoid = lifecycle.clone({voided.value->summary.id, 1});
        require(cloneVoid.ok() && cloneVoid.value->summary.status == manage::data::QuoteStatus::Draft,
                QStringLiteral("void quote can be cloned"));
        require(lifecycle.deleteDraft({
                    cloneVoid.value->summary.id,
                    cloneVoid.value->summary.revision,
                    1,
                }).ok(),
                QStringLiteral("cloned void draft can be deleted"));

        cleanup(database, customerId, bomId, materialId);
    } catch (...) {
        cleanup(database, customerId, bomId, materialId);
        throw;
    }
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    if (qEnvironmentVariable("MANAGE_TEST_MYSQL") != QStringLiteral("1")) {
        std::cout << "[SKIP] set MANAGE_TEST_MYSQL=1 to run the real MySQL quote test\n";
        return EXIT_SUCCESS;
    }

    const auto config = manage::data::DatabaseConfig::fromEnvironment();
    if (!config.databaseName.endsWith(QStringLiteral("_test"), Qt::CaseInsensitive)) {
        std::cerr << "[REFUSE] MANAGE_DB_NAME must end with _test for the real quote test\n";
        return EXIT_FAILURE;
    }

    try {
        manage::data::DatabaseConnection connection(config);
        QString error;
        require(connection.open(&error), error);
        manage::data::MigrationRunner migrations(connection.database());
        manage::data::MigrationReport report;
        require(migrations.migrate(&report, &error), error);
        require(report.currentVersion == 10, QStringLiteral("quote schema version must be 10"));
        runLifecycle(connection.database());
        std::cout << "[PASS] real MySQL quote lifecycle, snapshots and transactions\n";
        runSupplierSnapshot(connection.database());
        std::cout << "[PASS] real MySQL quote supplier snapshot\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
