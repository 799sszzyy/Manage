#include "manage/data/mysql_bom_repository.h"

#include <QHash>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <utility>

namespace manage::data {
namespace {

QString sqlText(const QString& value) {
    return value.isNull() ? QStringLiteral("") : value;
}

BomTemplateSummary readSummary(QSqlQuery& query) {
    BomTemplateSummary summary;
    summary.id = query.value(0).toLongLong();
    summary.code = query.value(1).toString();
    summary.name = query.value(2).toString();
    summary.description = query.value(3).toString();
    summary.isEnabled = query.value(4).toBool();
    summary.revision = query.value(5).toInt();
    return summary;
}

void bindSearch(QSqlQuery& query, const BomSearchQuery& search) {
    if (!search.search.isEmpty()) {
        query.bindValue(QStringLiteral(":search"),
                        QStringLiteral("%%1%").arg(search.search));
    }
    if (search.enabled.has_value()) {
        query.bindValue(QStringLiteral(":enabled"), *search.enabled);
    }
}

QString whereClause(const BomSearchQuery& search) {
    QStringList filters;
    if (!search.search.isEmpty()) {
        filters.push_back(QStringLiteral("(code LIKE :search OR name LIKE :search)"));
    }
    if (search.enabled.has_value()) {
        filters.push_back(QStringLiteral("is_enabled = :enabled"));
    }
    return filters.isEmpty()
               ? QString()
               : QStringLiteral(" WHERE ") + filters.join(QStringLiteral(" AND "));
}

} // namespace

MySqlBomRepository::MySqlBomRepository(QSqlDatabase database)
    : database_(std::move(database)) {}

BomRepositoryStatus MySqlBomRepository::list(
    const BomSearchQuery& search,
    BomPage& result,
    QString& errorMessage
) {
    errorMessage.clear();
    const auto where = whereClause(search);

    QSqlQuery countQuery(database_);
    if (!countQuery.prepare(
            QStringLiteral("SELECT COUNT(*) FROM bom_templates") + where
        )) {
        errorMessage = countQuery.lastError().text();
        return BomRepositoryStatus::DatabaseError;
    }
    bindSearch(countQuery, search);
    if (!countQuery.exec() || !countQuery.next()) {
        errorMessage = countQuery.lastError().text();
        return BomRepositoryStatus::DatabaseError;
    }

    result = {};
    result.total = countQuery.value(0).toLongLong();
    result.page = search.page;
    result.pageSize = search.pageSize;

    QSqlQuery dataQuery(database_);
    const auto sql = QStringLiteral(
        "SELECT id, code, name, description, is_enabled, revision "
        "FROM bom_templates"
    ) + where + QStringLiteral(
        " ORDER BY name, id LIMIT :limit OFFSET :offset"
    );
    if (!dataQuery.prepare(sql)) {
        errorMessage = dataQuery.lastError().text();
        return BomRepositoryStatus::DatabaseError;
    }
    bindSearch(dataQuery, search);
    dataQuery.bindValue(QStringLiteral(":limit"), search.pageSize);
    dataQuery.bindValue(
        QStringLiteral(":offset"),
        static_cast<qint64>(search.page - 1) * search.pageSize
    );
    if (!dataQuery.exec()) {
        errorMessage = dataQuery.lastError().text();
        return BomRepositoryStatus::DatabaseError;
    }
    while (dataQuery.next()) {
        result.items.push_back(readSummary(dataQuery));
    }
    return BomRepositoryStatus::Success;
}

BomRepositoryStatus MySqlBomRepository::getById(
    qint64 id,
    std::optional<BomTemplate>& result,
    QString& errorMessage
) {
    BomTemplate loaded;
    const auto status = load(id, loaded, errorMessage);
    if (status == BomRepositoryStatus::Success) {
        result = std::move(loaded);
    } else {
        result.reset();
    }
    return status;
}

BomRepositoryStatus MySqlBomRepository::lookupMaterials(
    const std::vector<qint64>& ids,
    std::vector<MaterialReference>& result,
    QString& errorMessage
) {
    result.clear();
    errorMessage.clear();
    if (ids.empty()) {
        return BomRepositoryStatus::Success;
    }

    QSet<qint64> uniqueIds;
    QStringList placeholders;
    for (const auto id : ids) {
        if (!uniqueIds.contains(id)) {
            uniqueIds.insert(id);
            placeholders.push_back(QStringLiteral(":id%1").arg(placeholders.size()));
        }
    }

    QSqlQuery query(database_);
    if (!query.prepare(QStringLiteral(
            "SELECT id, is_enabled FROM materials WHERE id IN (%1)"
        ).arg(placeholders.join(QLatin1Char(','))))) {
        errorMessage = query.lastError().text();
        return BomRepositoryStatus::DatabaseError;
    }
    qsizetype parameterIndex = 0;
    for (const auto id : uniqueIds) {
        query.bindValue(
            QStringLiteral(":id%1").arg(parameterIndex++),
            id
        );
    }
    if (!query.exec()) {
        errorMessage = query.lastError().text();
        return BomRepositoryStatus::DatabaseError;
    }
    while (query.next()) {
        result.push_back({query.value(0).toLongLong(), query.value(1).toBool()});
    }
    return BomRepositoryStatus::Success;
}

BomRepositoryStatus MySqlBomRepository::create(
    const NewBomTemplate& command,
    BomTemplate& result,
    QString& errorMessage
) {
    errorMessage.clear();
    if (!database_.transaction()) {
        errorMessage = database_.lastError().text();
        return BomRepositoryStatus::DatabaseError;
    }

    const auto materialStatus = lockAndValidateMaterials(
        command.items,
        errorMessage
    );
    if (materialStatus != BomRepositoryStatus::Success) {
        database_.rollback();
        return materialStatus;
    }

    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO bom_templates (code, name, description, is_enabled) "
        "VALUES (:code, :name, :description, :enabled)"
    ));
    query.bindValue(QStringLiteral(":code"), command.code);
    query.bindValue(QStringLiteral(":name"), command.name);
    query.bindValue(
        QStringLiteral(":description"),
        sqlText(command.description)
    );
    query.bindValue(QStringLiteral(":enabled"), command.isEnabled);
    if (!query.exec()) {
        const auto error = query.lastError();
        database_.rollback();
        errorMessage = error.text();
        return classifyWriteError(error.nativeErrorCode(), errorMessage);
    }

    const auto id = query.lastInsertId().toLongLong();
    if (!insertItems(id, command.items, errorMessage)) {
        database_.rollback();
        return BomRepositoryStatus::DatabaseError;
    }
    const auto loadStatus = load(id, result, errorMessage);
    if (loadStatus != BomRepositoryStatus::Success) {
        database_.rollback();
        return loadStatus;
    }
    if (!database_.commit()) {
        errorMessage = database_.lastError().text();
        database_.rollback();
        return BomRepositoryStatus::DatabaseError;
    }
    return BomRepositoryStatus::Success;
}

BomRepositoryStatus MySqlBomRepository::update(
    const UpdateBomTemplate& command,
    BomTemplate& result,
    QString& errorMessage
) {
    errorMessage.clear();
    if (!database_.transaction()) {
        errorMessage = database_.lastError().text();
        return BomRepositoryStatus::DatabaseError;
    }

    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE bom_templates SET code = :code, name = :name, "
        "description = :description, revision = revision + 1 "
        "WHERE id = :id AND revision = :revision"
    ));
    query.bindValue(QStringLiteral(":code"), command.code);
    query.bindValue(QStringLiteral(":name"), command.name);
    query.bindValue(
        QStringLiteral(":description"),
        sqlText(command.description)
    );
    query.bindValue(QStringLiteral(":id"), command.id);
    query.bindValue(QStringLiteral(":revision"), command.expectedRevision);
    if (!query.exec()) {
        const auto error = query.lastError();
        database_.rollback();
        errorMessage = error.text();
        return classifyWriteError(error.nativeErrorCode(), errorMessage);
    }
    if (query.numRowsAffected() != 1) {
        const auto status = statusAfterMiss(command.id, errorMessage);
        database_.rollback();
        return status;
    }

    const auto loadStatus = load(command.id, result, errorMessage);
    if (loadStatus != BomRepositoryStatus::Success || !database_.commit()) {
        if (errorMessage.isEmpty()) {
            errorMessage = database_.lastError().text();
        }
        database_.rollback();
        return loadStatus == BomRepositoryStatus::Success
                   ? BomRepositoryStatus::DatabaseError
                   : loadStatus;
    }
    return BomRepositoryStatus::Success;
}

BomRepositoryStatus MySqlBomRepository::setEnabled(
    const SetBomEnabled& command,
    BomTemplate& result,
    QString& errorMessage
) {
    errorMessage.clear();
    if (!database_.transaction()) {
        errorMessage = database_.lastError().text();
        return BomRepositoryStatus::DatabaseError;
    }

    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE bom_templates SET is_enabled = :enabled, revision = revision + 1 "
        "WHERE id = :id AND revision = :revision"
    ));
    query.bindValue(QStringLiteral(":enabled"), command.isEnabled);
    query.bindValue(QStringLiteral(":id"), command.id);
    query.bindValue(QStringLiteral(":revision"), command.expectedRevision);
    if (!query.exec()) {
        errorMessage = query.lastError().text();
        database_.rollback();
        return BomRepositoryStatus::DatabaseError;
    }
    if (query.numRowsAffected() != 1) {
        const auto status = statusAfterMiss(command.id, errorMessage);
        database_.rollback();
        return status;
    }

    const auto loadStatus = load(command.id, result, errorMessage);
    if (loadStatus != BomRepositoryStatus::Success || !database_.commit()) {
        if (errorMessage.isEmpty()) {
            errorMessage = database_.lastError().text();
        }
        database_.rollback();
        return loadStatus == BomRepositoryStatus::Success
                   ? BomRepositoryStatus::DatabaseError
                   : loadStatus;
    }
    return BomRepositoryStatus::Success;
}

BomRepositoryStatus MySqlBomRepository::replaceItems(
    const ReplaceBomItems& command,
    BomTemplate& result,
    QString& errorMessage
) {
    errorMessage.clear();
    if (!database_.transaction()) {
        errorMessage = database_.lastError().text();
        return BomRepositoryStatus::DatabaseError;
    }

    const auto materialStatus = lockAndValidateMaterials(
        command.items,
        errorMessage
    );
    if (materialStatus != BomRepositoryStatus::Success) {
        database_.rollback();
        return materialStatus;
    }

    QSqlQuery versionQuery(database_);
    versionQuery.prepare(QStringLiteral(
        "UPDATE bom_templates SET revision = revision + 1 "
        "WHERE id = :id AND revision = :revision"
    ));
    versionQuery.bindValue(QStringLiteral(":id"), command.id);
    versionQuery.bindValue(QStringLiteral(":revision"), command.expectedRevision);
    if (!versionQuery.exec()) {
        errorMessage = versionQuery.lastError().text();
        database_.rollback();
        return BomRepositoryStatus::DatabaseError;
    }
    if (versionQuery.numRowsAffected() != 1) {
        const auto status = statusAfterMiss(command.id, errorMessage);
        database_.rollback();
        return status;
    }

    QSqlQuery deleteQuery(database_);
    deleteQuery.prepare(QStringLiteral(
        "DELETE FROM bom_items WHERE bom_template_id = :id"
    ));
    deleteQuery.bindValue(QStringLiteral(":id"), command.id);
    if (!deleteQuery.exec()) {
        errorMessage = deleteQuery.lastError().text();
        database_.rollback();
        return BomRepositoryStatus::DatabaseError;
    }
    if (!insertItems(command.id, command.items, errorMessage)) {
        database_.rollback();
        return BomRepositoryStatus::DatabaseError;
    }

    const auto loadStatus = load(command.id, result, errorMessage);
    if (loadStatus != BomRepositoryStatus::Success || !database_.commit()) {
        if (errorMessage.isEmpty()) {
            errorMessage = database_.lastError().text();
        }
        database_.rollback();
        return loadStatus == BomRepositoryStatus::Success
                   ? BomRepositoryStatus::DatabaseError
                   : loadStatus;
    }
    return BomRepositoryStatus::Success;
}

BomRepositoryStatus MySqlBomRepository::load(
    qint64 id,
    BomTemplate& result,
    QString& errorMessage
) {
    QSqlQuery templateQuery(database_);
    templateQuery.prepare(QStringLiteral(
        "SELECT id, code, name, description, is_enabled, revision "
        "FROM bom_templates WHERE id = :id"
    ));
    templateQuery.bindValue(QStringLiteral(":id"), id);
    if (!templateQuery.exec()) {
        errorMessage = templateQuery.lastError().text();
        return BomRepositoryStatus::DatabaseError;
    }
    if (!templateQuery.next()) {
        return BomRepositoryStatus::NotFound;
    }

    result = {};
    result.summary = readSummary(templateQuery);

    QSqlQuery itemQuery(database_);
    itemQuery.prepare(QStringLiteral(
        "SELECT bi.id, bi.line_no, bi.material_id, m.code, m.name, "
        "m.specification, m.unit, bi.quantity_micros, bi.notes "
        "FROM bom_items bi JOIN materials m ON m.id = bi.material_id "
        "WHERE bi.bom_template_id = :id ORDER BY bi.line_no"
    ));
    itemQuery.bindValue(QStringLiteral(":id"), id);
    if (!itemQuery.exec()) {
        errorMessage = itemQuery.lastError().text();
        return BomRepositoryStatus::DatabaseError;
    }
    while (itemQuery.next()) {
        result.items.push_back({
            itemQuery.value(0).toLongLong(),
            itemQuery.value(1).toInt(),
            itemQuery.value(2).toLongLong(),
            itemQuery.value(3).toString(),
            itemQuery.value(4).toString(),
            itemQuery.value(5).toString(),
            itemQuery.value(6).toString(),
            itemQuery.value(7).toLongLong(),
            itemQuery.value(8).toString(),
        });
    }
    return BomRepositoryStatus::Success;
}

BomRepositoryStatus MySqlBomRepository::classifyWriteError(
    const QString& nativeCode,
    const QString& message
) const {
    if (nativeCode == QStringLiteral("1062") ||
        message.contains(QStringLiteral("uq_bom_templates_code"),
                         Qt::CaseInsensitive)) {
        return BomRepositoryStatus::DuplicateCode;
    }
    return BomRepositoryStatus::DatabaseError;
}

BomRepositoryStatus MySqlBomRepository::statusAfterMiss(
    qint64 id,
    QString& errorMessage
) {
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("SELECT 1 FROM bom_templates WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec()) {
        errorMessage = query.lastError().text();
        return BomRepositoryStatus::DatabaseError;
    }
    return query.next() ? BomRepositoryStatus::Conflict
                        : BomRepositoryStatus::NotFound;
}

BomRepositoryStatus MySqlBomRepository::lockAndValidateMaterials(
    const std::vector<BomItemInput>& items,
    QString& errorMessage
) {
    if (items.empty()) {
        return BomRepositoryStatus::Success;
    }

    QSet<qint64> uniqueIds;
    QStringList placeholders;
    for (const auto& item : items) {
        if (!uniqueIds.contains(item.materialId)) {
            uniqueIds.insert(item.materialId);
            placeholders.push_back(
                QStringLiteral(":material%1").arg(placeholders.size())
            );
        }
    }

    QSqlQuery query(database_);
    if (!query.prepare(QStringLiteral(
            "SELECT id, is_enabled FROM materials WHERE id IN (%1) FOR SHARE"
        ).arg(placeholders.join(QLatin1Char(','))))) {
        errorMessage = query.lastError().text();
        return BomRepositoryStatus::DatabaseError;
    }
    qsizetype parameterIndex = 0;
    for (const auto id : uniqueIds) {
        query.bindValue(
            QStringLiteral(":material%1").arg(parameterIndex++),
            id
        );
    }
    if (!query.exec()) {
        errorMessage = query.lastError().text();
        return BomRepositoryStatus::DatabaseError;
    }

    QHash<qint64, bool> enabledById;
    while (query.next()) {
        enabledById.insert(query.value(0).toLongLong(), query.value(1).toBool());
    }
    for (const auto id : uniqueIds) {
        if (!enabledById.contains(id)) {
            errorMessage = QStringLiteral(
                "material %1 does not exist while saving the BOM"
            ).arg(id);
            return BomRepositoryStatus::InvalidMaterial;
        }
        if (!enabledById.value(id)) {
            errorMessage = QStringLiteral(
                "material %1 is disabled while saving the BOM"
            ).arg(id);
            return BomRepositoryStatus::InvalidMaterial;
        }
    }
    return BomRepositoryStatus::Success;
}

bool MySqlBomRepository::insertItems(
    qint64 bomId,
    const std::vector<BomItemInput>& items,
    QString& errorMessage
) {
    if (items.empty()) {
        return true;
    }

    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO bom_items "
        "(bom_template_id, line_no, material_id, quantity_micros, notes) "
        "VALUES (:bomId, :lineNo, :materialId, :quantityMicros, :notes)"
    ));
    for (const auto& item : items) {
        query.bindValue(QStringLiteral(":bomId"), bomId);
        query.bindValue(QStringLiteral(":lineNo"), item.lineNo);
        query.bindValue(QStringLiteral(":materialId"), item.materialId);
        query.bindValue(QStringLiteral(":quantityMicros"), item.quantityMicros);
        query.bindValue(
            QStringLiteral(":notes"),
            sqlText(item.notes.trimmed())
        );
        if (!query.exec()) {
            errorMessage = query.lastError().text();
            return false;
        }
    }
    return true;
}

} // namespace manage::data
