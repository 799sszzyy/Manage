#include "manage/data/process_step_repository.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <utility>

namespace manage::data {
namespace {

constexpr auto kProcessStepColumns =
    "id, code, name, labor_minutes, description, is_enabled, revision, "
    "created_at, updated_at";

RepositoryErrorCode sqlErrorCode(const QSqlError& error) {
    if (error.nativeErrorCode() == QStringLiteral("1062")) {
        return RepositoryErrorCode::Duplicate;
    }
    if (error.nativeErrorCode() == QStringLiteral("1406")) {
        return RepositoryErrorCode::Database;
    }
    return RepositoryErrorCode::Database;
}

QString sqlString(const QString& value) {
    return value.isNull() ? QString() : value;
}

ProcessStep readProcessStep(const QSqlQuery& query) {
    ProcessStep step;
    step.id = query.value(0).toLongLong();
    step.code = query.value(1).toString();
    step.name = query.value(2).toString();
    step.laborMinutes = query.value(3).toInt();
    step.description = query.value(4).toString();
    step.isEnabled = query.value(5).toBool();
    step.revision = query.value(6).toUInt();
    step.createdAt = query.value(7).toDateTime();
    step.updatedAt = query.value(8).toDateTime();
    return step;
}

void bindProcessStepDraft(QSqlQuery* query, const ProcessStepDraft& draft) {
    query->bindValue(QStringLiteral(":code"), sqlString(draft.code));
    query->bindValue(QStringLiteral(":name"), sqlString(draft.name));
    query->bindValue(QStringLiteral(":laborMinutes"), draft.laborMinutes);
    query->bindValue(QStringLiteral(":description"), sqlString(draft.description));
    query->bindValue(QStringLiteral(":isEnabled"), draft.isEnabled);
}

} // namespace

MySqlProcessStepRepository::MySqlProcessStepRepository(QSqlDatabase database)
    : database_(std::move(database)) {}

void MySqlProcessStepRepository::clearError(RepositoryError* error) {
    if (error != nullptr) {
        *error = {};
    }
}

void MySqlProcessStepRepository::setError(
    RepositoryError* error,
    RepositoryErrorCode code,
    const QString& message
) {
    if (error != nullptr) {
        *error = {code, message};
    }
}

bool MySqlProcessStepRepository::recordExists(
    std::int64_t id,
    bool* exists,
    RepositoryError* error
) {
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT 1 FROM process_steps WHERE id = :id"
    ));
    query.bindValue(QStringLiteral(":id"), QVariant::fromValue<qlonglong>(id));
    if (!query.exec()) {
        setError(error, RepositoryErrorCode::Database, query.lastError().text());
        return false;
    }
    *exists = query.next();
    return true;
}

bool MySqlProcessStepRepository::list(
    const PageQuery& query,
    ProcessStepPage* page,
    RepositoryError* error
) {
    clearError(error);
    if (page == nullptr) {
        setError(error, RepositoryErrorCode::Database, QStringLiteral("page output is null"));
        return false;
    }

    QStringList conditions;
    if (!query.search.trimmed().isEmpty()) {
        conditions.append(QStringLiteral(
            "(LOCATE(:search, code) > 0 OR LOCATE(:search, name) > 0)"
        ));
    }
    if (query.enabled.has_value()) {
        conditions.append(QStringLiteral("is_enabled = :enabled"));
    }
    const auto where = conditions.isEmpty()
                           ? QString()
                           : QStringLiteral(" WHERE %1").arg(
                                 conditions.join(QStringLiteral(" AND "))
                             );
    const auto bindFilters = [&](QSqlQuery& sqlQuery) {
        if (!query.search.trimmed().isEmpty()) {
            sqlQuery.bindValue(QStringLiteral(":search"), query.search.trimmed());
        }
        if (query.enabled.has_value()) {
            sqlQuery.bindValue(QStringLiteral(":enabled"), *query.enabled);
        }
    };

    QSqlQuery count(database_);
    count.prepare(QStringLiteral("SELECT COUNT(*) FROM process_steps%1").arg(where));
    bindFilters(count);
    if (!count.exec() || !count.next()) {
        setError(error, RepositoryErrorCode::Database, count.lastError().text());
        return false;
    }

    ProcessStepPage result;
    result.total = count.value(0).toLongLong();
    result.page = query.page;
    result.pageSize = query.pageSize;
    const auto offset = (static_cast<std::int64_t>(query.page) - 1) * query.pageSize;

    QSqlQuery items(database_);
    items.prepare(QStringLiteral(
        "SELECT %1 FROM process_steps%2 ORDER BY id DESC LIMIT %3 OFFSET %4"
    ).arg(QString::fromLatin1(kProcessStepColumns), where)
        .arg(query.pageSize)
        .arg(offset));
    bindFilters(items);
    if (!items.exec()) {
        setError(error, RepositoryErrorCode::Database, items.lastError().text());
        return false;
    }
    while (items.next()) {
        result.items.push_back(readProcessStep(items));
    }
    *page = std::move(result);
    return true;
}

bool MySqlProcessStepRepository::find(
    std::int64_t id,
    ProcessStep* step,
    RepositoryError* error
) {
    clearError(error);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT %1 FROM process_steps WHERE id = :id"
    ).arg(QString::fromLatin1(kProcessStepColumns)));
    query.bindValue(QStringLiteral(":id"), QVariant::fromValue<qlonglong>(id));
    if (!query.exec()) {
        setError(error, RepositoryErrorCode::Database, query.lastError().text());
        return false;
    }
    if (!query.next()) {
        setError(
            error,
            RepositoryErrorCode::NotFound,
            QStringLiteral("process step not found")
        );
        return false;
    }
    if (step != nullptr) {
        *step = readProcessStep(query);
    }
    return true;
}

bool MySqlProcessStepRepository::create(
    const ProcessStepDraft& draft,
    ProcessStep* step,
    RepositoryError* error
) {
    clearError(error);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO process_steps "
        "(code, name, labor_minutes, description, is_enabled) "
        "VALUES (:code, :name, :laborMinutes, :description, :isEnabled)"
    ));
    bindProcessStepDraft(&query, draft);
    if (!query.exec()) {
        setError(error, sqlErrorCode(query.lastError()), query.lastError().text());
        return false;
    }
    return find(query.lastInsertId().toLongLong(), step, error);
}

bool MySqlProcessStepRepository::update(
    std::int64_t id,
    std::uint32_t expectedRevision,
    const ProcessStepDraft& draft,
    ProcessStep* step,
    RepositoryError* error
) {
    clearError(error);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE process_steps SET code = :code, name = :name, "
        "labor_minutes = :laborMinutes, description = :description, "
        "is_enabled = :isEnabled, revision = revision + 1 "
        "WHERE id = :id AND revision = :revision"
    ));
    bindProcessStepDraft(&query, draft);
    query.bindValue(QStringLiteral(":id"), QVariant::fromValue<qlonglong>(id));
    query.bindValue(QStringLiteral(":revision"), expectedRevision);
    if (!query.exec()) {
        setError(error, sqlErrorCode(query.lastError()), query.lastError().text());
        return false;
    }
    if (query.numRowsAffected() == 0) {
        bool exists = false;
        if (!recordExists(id, &exists, error)) {
            return false;
        }
        setError(
            error,
            exists ? RepositoryErrorCode::RevisionConflict
                   : RepositoryErrorCode::NotFound,
            exists ? QStringLiteral("process step revision conflict")
                   : QStringLiteral("process step not found")
        );
        return false;
    }
    return find(id, step, error);
}

bool MySqlProcessStepRepository::setEnabled(
    std::int64_t id,
    std::uint32_t expectedRevision,
    bool enabled,
    ProcessStep* step,
    RepositoryError* error
) {
    clearError(error);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE process_steps SET is_enabled = :enabled, revision = revision + 1 "
        "WHERE id = :id AND revision = :revision"
    ));
    query.bindValue(QStringLiteral(":enabled"), enabled);
    query.bindValue(QStringLiteral(":id"), QVariant::fromValue<qlonglong>(id));
    query.bindValue(QStringLiteral(":revision"), expectedRevision);
    if (!query.exec()) {
        setError(error, sqlErrorCode(query.lastError()), query.lastError().text());
        return false;
    }
    if (query.numRowsAffected() == 0) {
        bool exists = false;
        if (!recordExists(id, &exists, error)) {
            return false;
        }
        setError(
            error,
            exists ? RepositoryErrorCode::RevisionConflict
                   : RepositoryErrorCode::NotFound,
            exists ? QStringLiteral("process step revision conflict")
                   : QStringLiteral("process step not found")
        );
        return false;
    }
    return find(id, step, error);
}

} // namespace manage::data
