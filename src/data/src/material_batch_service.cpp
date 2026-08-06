#include "manage/data/material_batch_service.h"

#include <QRegularExpression>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>

#include <utility>

namespace manage::data {
namespace {

void addError(
    MaterialBatchResult& result,
    int row,
    const QString& field,
    const QString& message
) {
    result.errors.push_back({row, field, message});
}

void normalize(MaterialDraft& draft) {
    draft.code = draft.code.trimmed();
    draft.name = draft.name.trimmed();
    draft.specification = draft.specification.trimmed();
    draft.unit = draft.unit.trimmed();
    draft.category = draft.category.trimmed();
}

void validateRow(MaterialBatchResult& result, const MaterialBatchRow& row) {
    static const QRegularExpression codePattern(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$")
    );
    const auto& draft = row.material;
    if (!codePattern.match(draft.code).hasMatch()) {
        addError(result, row.sourceRow, QStringLiteral("code"),
                 QStringLiteral("物料编码格式不正确"));
    }
    if (draft.name.isEmpty() || draft.name.size() > 200) {
        addError(result, row.sourceRow, QStringLiteral("name"),
                 QStringLiteral("物料名称应为 1-200 个字符"));
    }
    if (draft.specification.size() > 500) {
        addError(result, row.sourceRow, QStringLiteral("specification"),
                 QStringLiteral("规格最多 500 个字符"));
    }
    if (draft.unit.isEmpty() || draft.unit.size() > 32) {
        addError(result, row.sourceRow, QStringLiteral("unit"),
                 QStringLiteral("单位应为 1-32 个字符"));
    }
    if (draft.category.size() > 100) {
        addError(result, row.sourceRow, QStringLiteral("category"),
                 QStringLiteral("类别最多 100 个字符"));
    }
    if (draft.currentUnitPriceCents < 0) {
        addError(result, row.sourceRow, QStringLiteral("currentUnitPriceCents"),
                 QStringLiteral("当前单价不能为负数"));
    }
    if (row.supplierName.size() > 200) {
        addError(result, row.sourceRow, QStringLiteral("supplierName"),
                 QStringLiteral("供应商名称最多 200 个字符"));
    }
    if (row.leadDays < 0 || row.leadDays > 36'500) {
        addError(result, row.sourceRow, QStringLiteral("leadDays"),
                 QStringLiteral("供货周期应为 0-36500 的整数天数"));
    }
}

bool lookupExistingCodes(
    QSqlDatabase database,
    const std::vector<MaterialBatchRow>& rows,
    QSet<QString>& existing,
    QString& error
) {
    QStringList placeholders;
    placeholders.reserve(static_cast<qsizetype>(rows.size()));
    for (std::size_t index = 0; index < rows.size(); ++index) {
        placeholders.append(QStringLiteral("?"));
    }
    QSqlQuery query(database);
    query.prepare(
        QStringLiteral("SELECT code FROM materials WHERE code IN (%1)")
            .arg(placeholders.join(QStringLiteral(",")))
    );
    for (std::size_t index = 0; index < rows.size(); ++index) {
        query.bindValue(static_cast<int>(index), rows.at(index).material.code);
    }
    if (!query.exec()) {
        error = query.lastError().text();
        return false;
    }
    while (query.next()) {
        existing.insert(query.value(0).toString());
    }
    return true;
}

bool upsertRow(QSqlQuery& query, const MaterialDraft& draft) {
    query.bindValue(QStringLiteral(":code"), draft.code);
    query.bindValue(QStringLiteral(":name"), draft.name);
    query.bindValue(QStringLiteral(":specification"), draft.specification);
    query.bindValue(QStringLiteral(":unit"), draft.unit);
    query.bindValue(QStringLiteral(":category"), draft.category);
    query.bindValue(QStringLiteral(":price"), draft.currentUnitPriceCents);
    query.bindValue(QStringLiteral(":enabled"), draft.isEnabled);
    return query.exec();
}

} // namespace

MaterialBatchService::MaterialBatchService(QSqlDatabase database)
    : database_(std::move(database)) {}

MaterialBatchResult MaterialBatchService::importMaterials(
    std::vector<MaterialBatchRow> rows,
    bool validateOnly
) {
    MaterialBatchResult result;
    result.totalRows = static_cast<int>(rows.size());
    if (rows.empty()) {
        addError(result, 0, QStringLiteral("rows"), QStringLiteral("没有可导入的物料"));
        return result;
    }
    if (rows.size() > static_cast<std::size_t>(maximumRows)) {
        addError(result, 0, QStringLiteral("rows"),
                 QStringLiteral("一次最多导入 %1 行物料").arg(maximumRows));
        return result;
    }

    // Validate and normalize every row before the first SQL statement. This is
    // what guarantees that an invalid workbook cannot be partially imported.
    QSet<QString> workbookCodes;
    for (auto& row : rows) {
        normalize(row.material);
        validateRow(result, row);
        if (!row.material.code.isEmpty() && workbookCodes.contains(row.material.code)) {
            addError(result, row.sourceRow, QStringLiteral("code"),
                     QStringLiteral("同一批次内物料编码重复"));
        }
        workbookCodes.insert(row.material.code);
    }
    if (!result.errors.empty()) {
        return result;
    }
    if (!database_.isValid() || !database_.isOpen()) {
        addError(result, 0, QStringLiteral("database"), QStringLiteral("数据库连接不可用"));
        return result;
    }

    QSet<QString> existing;
    QString lookupError;
    if (!lookupExistingCodes(database_, rows, existing, lookupError)) {
        addError(result, 0, QStringLiteral("database"),
                 QStringLiteral("读取现有物料失败：%1").arg(lookupError));
        return result;
    }
    for (const auto& row : rows) {
        if (existing.contains(row.material.code)) {
            ++result.updateCount;
        } else {
            ++result.createCount;
        }
    }
    if (validateOnly) {
        return result;
    }

    // All upserts share one transaction: either every validated row becomes
    // visible, or the first database error rolls the complete batch back.
    if (!database_.transaction()) {
        addError(result, 0, QStringLiteral("database"),
                 QStringLiteral("无法开始导入事务：%1").arg(database_.lastError().text()));
        return result;
    }
    QSqlQuery upsert(database_);
    upsert.prepare(QStringLiteral(
        "INSERT INTO materials "
        "(code, name, specification, unit, category, current_unit_price_cents, is_enabled) "
        "VALUES (:code, :name, :specification, :unit, :category, :price, :enabled) "
        "ON DUPLICATE KEY UPDATE "
        "name = VALUES(name), specification = VALUES(specification), "
        "unit = VALUES(unit), category = VALUES(category), "
        "current_unit_price_cents = VALUES(current_unit_price_cents), "
        "is_enabled = VALUES(is_enabled), revision = revision + 1"
    ));

    // 供应商分支 upsert：导入行填写供应商名称时同步创建/更新供应商，
    // 并写入交货周期（天）。同一批中同一物料+供应商只处理一次。
    QSqlQuery supplierQuery(database_);
    supplierQuery.prepare(QStringLiteral(
        "INSERT INTO material_suppliers "
        "(material_id, supplier_name, contact_name, phone, is_default, is_enabled, lead_days) "
        "SELECT id, :supplierName, '', '', FALSE, TRUE, :leadDays FROM materials "
        "WHERE code = :code "
        "ON DUPLICATE KEY UPDATE lead_days = :leadDays, revision = revision + 1"
    ));
    QSet<QString> handledSupplierKeys;

    for (const auto& row : rows) {
        if (!upsertRow(upsert, row.material)) {
            const auto message = upsert.lastError().text();
            database_.rollback();
            addError(result, row.sourceRow, QStringLiteral("database"),
                     QStringLiteral("整批导入已回滚：%1").arg(message));
            return result;
        }
        upsert.finish();

        if (!row.supplierName.isEmpty()) {
            const auto key = QStringLiteral("%1|%2").arg(
                row.material.code, row.supplierName
            );
            if (handledSupplierKeys.contains(key)) {
                continue;
            }
            handledSupplierKeys.insert(key);
            supplierQuery.bindValue(QStringLiteral(":supplierName"), row.supplierName);
            supplierQuery.bindValue(QStringLiteral(":leadDays"), row.leadDays);
            supplierQuery.bindValue(QStringLiteral(":code"), row.material.code);
            if (!supplierQuery.exec()) {
                const auto message = supplierQuery.lastError().text();
                database_.rollback();
                addError(result, row.sourceRow, QStringLiteral("database"),
                         QStringLiteral("供应商分支导入已回滚：%1").arg(message));
                return result;
            }
            supplierQuery.finish();
        }
    }
    if (!database_.commit()) {
        const auto message = database_.lastError().text();
        database_.rollback();
        addError(result, 0, QStringLiteral("database"),
                 QStringLiteral("提交导入事务失败：%1").arg(message));
        return result;
    }
    result.committed = true;
    return result;
}

} // namespace manage::data
