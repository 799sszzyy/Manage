#include "manage/data/task_service.h"

#include <QDateTime>

#include <utility>

namespace manage::data {
namespace {

constexpr int kMaximumPage = 1'000'000;

template <typename T>
TaskResult<T> failed(TaskError error) {
    TaskResult<T> result;
    result.error = std::move(error);
    return result;
}

template <typename T>
TaskResult<T> succeeded(T value) {
    TaskResult<T> result;
    result.value = std::move(value);
    return result;
}

TaskError validationError(const QString& field, const QString& message) {
    return {TaskErrorCode::InvalidRequest, message, field};
}

QString trimmedOrEmpty(const QString& value) {
    return value.isNull() ? QStringLiteral("") : value.trimmed();
}

} // namespace

TaskService::TaskService(std::shared_ptr<TaskRepository> repository)
    : repository_(std::move(repository)) {}

std::optional<TaskError> TaskService::validatePage(TaskSearchQuery* query) {
    query->search = query->search.trimmed();
    if (query->page < 1 || query->page > kMaximumPage) {
        return validationError(
            QStringLiteral("page"),
            QStringLiteral("page must be between 1 and 1000000")
        );
    }
    if (query->pageSize < 1 || query->pageSize > 100) {
        return validationError(
            QStringLiteral("pageSize"),
            QStringLiteral("pageSize must be between 1 and 100")
        );
    }
    if (query->search.size() > 200) {
        return validationError(
            QStringLiteral("search"),
            QStringLiteral("search must not exceed 200 characters")
        );
    }
    return std::nullopt;
}

std::optional<TaskError> TaskService::validateDraft(TaskDraft* draft) {
    draft->title = trimmedOrEmpty(draft->title);
    draft->notes = trimmedOrEmpty(draft->notes);

    if (draft->dispatchedBy <= 0) {
        return validationError(
            QStringLiteral("dispatchedBy"),
            QStringLiteral("dispatchedBy must reference a sales account")
        );
    }
    if (draft->assignedEngineerId <= 0) {
        return validationError(
            QStringLiteral("assignedEngineerId"),
            QStringLiteral("assignedEngineerId must reference an engineer account")
        );
    }
    if (draft->assignedEngineerId == draft->dispatchedBy) {
        return validationError(
            QStringLiteral("assignedEngineerId"),
            QStringLiteral("assigned engineer must differ from the dispatcher")
        );
    }
    if (draft->title.size() > 200) {
        return validationError(
            QStringLiteral("title"),
            QStringLiteral("title must not exceed 200 characters")
        );
    }
    if (draft->notes.size() > 1000) {
        return validationError(
            QStringLiteral("notes"),
            QStringLiteral("notes must not exceed 1000 characters")
        );
    }
    if (draft->expectedCompletionAt.has_value()) {
        // 预期完成时间必须携带时区信息；服务端以 UTC 持久化。
        if (!draft->expectedCompletionAt->isValid()) {
            return validationError(
                QStringLiteral("expectedCompletionAt"),
                QStringLiteral("expectedCompletionAt is not a valid datetime")
            );
        }
    }
    return std::nullopt;
}

bool TaskService::isValidTransition(TaskStatus from, TaskStatus to) {
    if (from == to) {
        return true;
    }
    switch (from) {
    case TaskStatus::Dispatched:
        return to == TaskStatus::InProgress || to == TaskStatus::Cancelled;
    case TaskStatus::InProgress:
        return to == TaskStatus::Completed || to == TaskStatus::Cancelled;
    case TaskStatus::Completed:
    case TaskStatus::Cancelled:
        return false;  // 终态不可再迁移
    }
    return false;
}

TaskError TaskService::mapRepositoryError(const RepositoryError& error) {
    switch (error.code) {
    case RepositoryErrorCode::NotFound:
        return {TaskErrorCode::NotFound, error.message, {}};
    case RepositoryErrorCode::RevisionConflict:
        return {TaskErrorCode::RevisionConflict, error.message, {}};
    case RepositoryErrorCode::Duplicate:
        return {TaskErrorCode::DuplicateCode, error.message, QStringLiteral("taskNumber")};
    case RepositoryErrorCode::None:
    case RepositoryErrorCode::Database:
        return {TaskErrorCode::Database, error.message, {}};
    }
    return {TaskErrorCode::Database, error.message, {}};
}

QString TaskService::generateTaskNumber() {
    // 任务编号：TASK-yyyyMMddHHmmsszzz(UTC)。毫秒级精度足以避免常规并发冲突，
    // 唯一性最终由数据库 uq_tasks_number 约束兜底。
    return QStringLiteral("TASK-%1").arg(
        QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddHHmmsszzz"))
    );
}

TaskResult<TaskPage> TaskService::list(TaskSearchQuery query) const {
    if (const auto error = validatePage(&query); error.has_value()) {
        return failed<TaskPage>(*error);
    }
    TaskPage page;
    RepositoryError repositoryError;
    if (!repository_->list(query, &page, &repositoryError)) {
        return failed<TaskPage>(mapRepositoryError(repositoryError));
    }
    return succeeded(std::move(page));
}

TaskResult<Task> TaskService::get(std::int64_t id) const {
    if (id <= 0) {
        return failed<Task>(validationError(
            QStringLiteral("id"),
            QStringLiteral("id must be greater than zero")
        ));
    }
    Task task;
    RepositoryError repositoryError;
    if (!repository_->find(id, &task, &repositoryError)) {
        return failed<Task>(mapRepositoryError(repositoryError));
    }
    return succeeded(std::move(task));
}

TaskResult<Task> TaskService::create(TaskDraft draft) const {
    if (const auto error = validateDraft(&draft); error.has_value()) {
        return failed<Task>(*error);
    }
    // 任务编号由服务端统一生成，覆盖调用方传入的任何值。
    draft.taskNumber = generateTaskNumber();
    Task task;
    RepositoryError repositoryError;
    if (!repository_->create(draft, &task, &repositoryError)) {
        return failed<Task>(mapRepositoryError(repositoryError));
    }
    return succeeded(std::move(task));
}

TaskResult<Task> TaskService::update(
    std::int64_t id,
    std::uint32_t expectedRevision,
    TaskDraft draft
) const {
    if (id <= 0 || expectedRevision == 0) {
        return failed<Task>(validationError(
            id <= 0 ? QStringLiteral("id") : QStringLiteral("revision"),
            QStringLiteral("id and revision must be positive integers")
        ));
    }
    if (const auto error = validateDraft(&draft); error.has_value()) {
        return failed<Task>(*error);
    }
    // 编辑不改动任务编号：读取现值后原样写回，避免编号被篡改。
    Task existing;
    RepositoryError findError;
    if (!repository_->find(id, &existing, &findError)) {
        return failed<Task>(mapRepositoryError(findError));
    }
    draft.taskNumber = existing.taskNumber;

    Task task;
    RepositoryError repositoryError;
    if (!repository_->update(id, expectedRevision, draft, &task, &repositoryError)) {
        return failed<Task>(mapRepositoryError(repositoryError));
    }
    return succeeded(std::move(task));
}

TaskResult<Task> TaskService::setStatus(
    std::int64_t id,
    std::uint32_t expectedRevision,
    TaskStatus target
) const {
    if (id <= 0 || expectedRevision == 0) {
        return failed<Task>(validationError(
            id <= 0 ? QStringLiteral("id") : QStringLiteral("revision"),
            QStringLiteral("id and revision must be positive integers")
        ));
    }
    Task existing;
    RepositoryError findError;
    if (!repository_->find(id, &existing, &findError)) {
        return failed<Task>(mapRepositoryError(findError));
    }
    if (!isValidTransition(existing.status, target)) {
        return failed<Task>(TaskError{
            TaskErrorCode::InvalidTransition,
            QStringLiteral(
                "task status cannot transition from %1 to %2"
            ).arg(taskStatusCode(existing.status), taskStatusCode(target)),
            QStringLiteral("status")
        });
    }
    Task task;
    RepositoryError repositoryError;
    if (!repository_->setStatus(id, expectedRevision, target, &task, &repositoryError)) {
        return failed<Task>(mapRepositoryError(repositoryError));
    }
    return succeeded(std::move(task));
}

TaskResult<Task> TaskService::setQuoteId(
    std::int64_t id,
    std::uint32_t expectedRevision,
    const std::optional<std::int64_t>& quoteId
) const {
    if (id <= 0 || expectedRevision == 0) {
        return failed<Task>(validationError(
            id <= 0 ? QStringLiteral("id") : QStringLiteral("revision"),
            QStringLiteral("id and revision must be positive integers")
        ));
    }
    Task task;
    RepositoryError repositoryError;
    if (!repository_->setQuoteId(id, expectedRevision, quoteId, &task, &repositoryError)) {
        return failed<Task>(mapRepositoryError(repositoryError));
    }
    return succeeded(std::move(task));
}

} // namespace manage::data
