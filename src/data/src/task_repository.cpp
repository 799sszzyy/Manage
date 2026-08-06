#include "manage/data/task_repository.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <utility>

namespace manage::data {
namespace {

constexpr auto kTaskColumns =
    "id, task_number, customer_id, dispatched_by, assigned_engineer_id, "
    "expected_completion_at, status, title, notes, quote_id, revision, "
    "created_at, updated_at";

RepositoryErrorCode sqlErrorCode(const QSqlError& error) {
    if (error.nativeErrorCode() == QStringLiteral("1062")) {
        return RepositoryErrorCode::Duplicate;
    }
    return RepositoryErrorCode::Database;
}

QString sqlString(const QString& value) {
    return value.isNull() ? QString() : value;
}

// 可空整数字段绑定：nullopt 写入 NULL。
QVariant optionalInt64(const std::optional<std::int64_t>& value) {
    return value.has_value()
        ? QVariant::fromValue<qlonglong>(*value)
        : QVariant();
}

// 可空时间字段绑定：nullopt 写入 NULL。
QVariant optionalDateTime(const std::optional<QDateTime>& value) {
    return value.has_value() ? QVariant(value->toUTC()) : QVariant();
}

Task readTask(const QSqlQuery& query) {
    Task task;
    task.id = query.value(0).toLongLong();
    task.taskNumber = query.value(1).toString();
    task.customerId = query.value(2).isNull()
        ? std::optional<std::int64_t>{}
        : std::optional<std::int64_t>{query.value(2).toLongLong()};
    task.dispatchedBy = query.value(3).toLongLong();
    task.assignedEngineerId = query.value(4).toLongLong();
    task.expectedCompletionAt = query.value(5).isNull()
        ? std::optional<QDateTime>{}
        : std::optional<QDateTime>{query.value(5).toDateTime()};
    task.status = taskStatusFromCode(query.value(6).toString())
        .value_or(TaskStatus::Dispatched);
    task.title = query.value(7).toString();
    task.notes = query.value(8).toString();
    task.quoteId = query.value(9).isNull()
        ? std::optional<std::int64_t>{}
        : std::optional<std::int64_t>{query.value(9).toLongLong()};
    task.revision = query.value(10).toUInt();
    task.createdAt = query.value(11).toDateTime();
    task.updatedAt = query.value(12).toDateTime();
    return task;
}

void bindTaskDraft(QSqlQuery* query, const TaskDraft& draft) {
    query->bindValue(QStringLiteral(":taskNumber"), sqlString(draft.taskNumber));
    query->bindValue(QStringLiteral(":customerId"), optionalInt64(draft.customerId));
    query->bindValue(
        QStringLiteral(":dispatchedBy"),
        QVariant::fromValue<qlonglong>(draft.dispatchedBy)
    );
    query->bindValue(
        QStringLiteral(":assignedEngineerId"),
        QVariant::fromValue<qlonglong>(draft.assignedEngineerId)
    );
    query->bindValue(
        QStringLiteral(":expectedCompletionAt"),
        optionalDateTime(draft.expectedCompletionAt)
    );
    query->bindValue(QStringLiteral(":title"), sqlString(draft.title));
    query->bindValue(QStringLiteral(":notes"), sqlString(draft.notes));
}

} // namespace

MySqlTaskRepository::MySqlTaskRepository(QSqlDatabase database)
    : database_(std::move(database)) {}

void MySqlTaskRepository::clearError(RepositoryError* error) {
    if (error != nullptr) {
        *error = {};
    }
}

void MySqlTaskRepository::setError(
    RepositoryError* error,
    RepositoryErrorCode code,
    const QString& message
) {
    if (error != nullptr) {
        *error = {code, message};
    }
}

bool MySqlTaskRepository::recordExists(
    std::int64_t id,
    bool* exists,
    RepositoryError* error
) {
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("SELECT 1 FROM tasks WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), QVariant::fromValue<qlonglong>(id));
    if (!query.exec()) {
        setError(error, RepositoryErrorCode::Database, query.lastError().text());
        return false;
    }
    *exists = query.next();
    return true;
}

bool MySqlTaskRepository::list(
    const TaskSearchQuery& query,
    TaskPage* page,
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
            "(LOCATE(:search, task_number) > 0 OR LOCATE(:search, title) > 0)"
        ));
    }
    if (query.status.has_value()) {
        conditions.append(QStringLiteral("status = :status"));
    }
    if (query.assignedEngineerId.has_value()) {
        conditions.append(QStringLiteral("assigned_engineer_id = :engineer"));
    }
    if (query.dispatchedBy.has_value()) {
        conditions.append(QStringLiteral("dispatched_by = :dispatcher"));
    }
    const auto where = conditions.isEmpty()
        ? QString()
        : QStringLiteral(" WHERE %1").arg(conditions.join(QStringLiteral(" AND ")));
    const auto bindFilters = [&](QSqlQuery& sqlQuery) {
        if (!query.search.trimmed().isEmpty()) {
            sqlQuery.bindValue(QStringLiteral(":search"), query.search.trimmed());
        }
        if (query.status.has_value()) {
            sqlQuery.bindValue(
                QStringLiteral(":status"), taskStatusCode(*query.status)
            );
        }
        if (query.assignedEngineerId.has_value()) {
            sqlQuery.bindValue(
                QStringLiteral(":engineer"),
                QVariant::fromValue<qlonglong>(*query.assignedEngineerId)
            );
        }
        if (query.dispatchedBy.has_value()) {
            sqlQuery.bindValue(
                QStringLiteral(":dispatcher"),
                QVariant::fromValue<qlonglong>(*query.dispatchedBy)
            );
        }
    };

    QSqlQuery count(database_);
    count.prepare(QStringLiteral("SELECT COUNT(*) FROM tasks%1").arg(where));
    bindFilters(count);
    if (!count.exec() || !count.next()) {
        setError(error, RepositoryErrorCode::Database, count.lastError().text());
        return false;
    }

    TaskPage result;
    result.total = count.value(0).toLongLong();
    result.page = query.page;
    result.pageSize = query.pageSize;
    const auto offset = (static_cast<std::int64_t>(query.page) - 1) * query.pageSize;

    QSqlQuery items(database_);
    items.prepare(QStringLiteral(
        "SELECT %1 FROM tasks%2 ORDER BY id DESC LIMIT %3 OFFSET %4"
    ).arg(QString::fromLatin1(kTaskColumns), where)
        .arg(query.pageSize)
        .arg(offset));
    bindFilters(items);
    if (!items.exec()) {
        setError(error, RepositoryErrorCode::Database, items.lastError().text());
        return false;
    }
    while (items.next()) {
        result.items.push_back(readTask(items));
    }
    *page = std::move(result);
    return true;
}

bool MySqlTaskRepository::find(
    std::int64_t id,
    Task* task,
    RepositoryError* error
) {
    clearError(error);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT %1 FROM tasks WHERE id = :id"
    ).arg(QString::fromLatin1(kTaskColumns)));
    query.bindValue(QStringLiteral(":id"), QVariant::fromValue<qlonglong>(id));
    if (!query.exec()) {
        setError(error, RepositoryErrorCode::Database, query.lastError().text());
        return false;
    }
    if (!query.next()) {
        setError(error, RepositoryErrorCode::NotFound, QStringLiteral("task not found"));
        return false;
    }
    if (task != nullptr) {
        *task = readTask(query);
    }
    return true;
}

bool MySqlTaskRepository::create(
    const TaskDraft& draft,
    Task* task,
    RepositoryError* error
) {
    clearError(error);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO tasks "
        "(task_number, customer_id, dispatched_by, assigned_engineer_id, "
        "expected_completion_at, status, title, notes) "
        "VALUES (:taskNumber, :customerId, :dispatchedBy, :assignedEngineerId, "
        ":expectedCompletionAt, 'dispatched', :title, :notes)"
    ));
    bindTaskDraft(&query, draft);
    if (!query.exec()) {
        setError(error, sqlErrorCode(query.lastError()), query.lastError().text());
        return false;
    }
    return find(query.lastInsertId().toLongLong(), task, error);
}

bool MySqlTaskRepository::update(
    std::int64_t id,
    std::uint32_t expectedRevision,
    const TaskDraft& draft,
    Task* task,
    RepositoryError* error
) {
    clearError(error);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE tasks SET task_number = :taskNumber, customer_id = :customerId, "
        "dispatched_by = :dispatchedBy, assigned_engineer_id = :assignedEngineerId, "
        "expected_completion_at = :expectedCompletionAt, title = :title, "
        "notes = :notes, revision = revision + 1 "
        "WHERE id = :id AND revision = :revision"
    ));
    bindTaskDraft(&query, draft);
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
            exists ? QStringLiteral("task revision conflict")
                   : QStringLiteral("task not found")
        );
        return false;
    }
    return find(id, task, error);
}

bool MySqlTaskRepository::setStatus(
    std::int64_t id,
    std::uint32_t expectedRevision,
    TaskStatus status,
    Task* task,
    RepositoryError* error
) {
    clearError(error);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE tasks SET status = :status, revision = revision + 1 "
        "WHERE id = :id AND revision = :revision"
    ));
    query.bindValue(QStringLiteral(":status"), taskStatusCode(status));
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
            exists ? QStringLiteral("task revision conflict")
                   : QStringLiteral("task not found")
        );
        return false;
    }
    return find(id, task, error);
}

bool MySqlTaskRepository::setQuoteId(
    std::int64_t id,
    std::uint32_t expectedRevision,
    const std::optional<std::int64_t>& quoteId,
    Task* task,
    RepositoryError* error
) {
    clearError(error);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE tasks SET quote_id = :quoteId, revision = revision + 1 "
        "WHERE id = :id AND revision = :revision"
    ));
    query.bindValue(QStringLiteral(":quoteId"), optionalInt64(quoteId));
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
            exists ? QStringLiteral("task revision conflict")
                   : QStringLiteral("task not found")
        );
        return false;
    }
    return find(id, task, error);
}

} // namespace manage::data
