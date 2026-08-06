#pragma once

#include <QDateTime>
#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

namespace manage::data {

// 任务状态机：dispatched(已派发) -> in_progress(工程师处理中)
// -> completed(已完成)；任意非终态可 -> cancelled(已取消)。
enum class TaskStatus {
    Dispatched,
    InProgress,
    Completed,
    Cancelled,
};

inline QString taskStatusCode(TaskStatus status) {
    switch (status) {
    case TaskStatus::Dispatched:
        return QStringLiteral("dispatched");
    case TaskStatus::InProgress:
        return QStringLiteral("in_progress");
    case TaskStatus::Completed:
        return QStringLiteral("completed");
    case TaskStatus::Cancelled:
        return QStringLiteral("cancelled");
    }
    return QStringLiteral("dispatched");
}

inline std::optional<TaskStatus> taskStatusFromCode(const QString& code) {
    if (code == QStringLiteral("dispatched")) {
        return TaskStatus::Dispatched;
    }
    if (code == QStringLiteral("in_progress")) {
        return TaskStatus::InProgress;
    }
    if (code == QStringLiteral("completed")) {
        return TaskStatus::Completed;
    }
    if (code == QStringLiteral("cancelled")) {
        return TaskStatus::Cancelled;
    }
    return std::nullopt;
}

// 任务实体：销售派发给工程师的一条工作任务，是报价流程的起点。
struct Task final {
    std::int64_t id{};
    QString taskNumber;
    std::optional<std::int64_t> customerId;
    std::int64_t dispatchedBy{};        // 销售派单人(user id)
    std::int64_t assignedEngineerId{};  // 负责工程师(user id)
    std::optional<QDateTime> expectedCompletionAt;
    TaskStatus status{TaskStatus::Dispatched};
    QString title;
    QString notes;
    std::optional<std::int64_t> quoteId;  // 工程师发布报价后回填
    std::uint32_t revision{1};
    QDateTime createdAt;
    QDateTime updatedAt;
};

// 任务创建/更新草稿。taskNumber 由服务端自动生成，调用方无需填写。
struct TaskDraft final {
    QString taskNumber;  // 服务端生成；客户端传入会被覆盖
    std::optional<std::int64_t> customerId;
    std::int64_t dispatchedBy{};
    std::int64_t assignedEngineerId{};
    std::optional<QDateTime> expectedCompletionAt;
    QString title;
    QString notes;
};

// 任务分页结果。
struct TaskPage final {
    std::vector<Task> items;
    std::int64_t total{};
    int page{1};
    int pageSize{20};
};

// 任务查询参数。
struct TaskSearchQuery final {
    int page{1};
    int pageSize{20};
    QString search;
    std::optional<TaskStatus> status;
    std::optional<std::int64_t> assignedEngineerId;
    std::optional<std::int64_t> dispatchedBy;
};

enum class TaskErrorCode {
    None,
    InvalidRequest,
    NotFound,
    RevisionConflict,
    DuplicateCode,
    InvalidTransition,
    Database,
};

struct TaskError final {
    TaskErrorCode code{TaskErrorCode::InvalidRequest};
    QString message;
    QString field;
};

template <typename T>
struct TaskResult final {
    std::optional<T> value;
    std::optional<TaskError> error;

    bool ok() const { return value.has_value() && !error.has_value(); }
};

} // namespace manage::data
