#pragma once

#include "manage/data/task_models.h"
#include "manage/data/task_repository.h"

#include <cstdint>
#include <memory>

namespace manage::data {

// 任务派发服务：销售创建任务（自动生成任务编号）、工程师推进状态、
// 报价发布后回填关联。状态迁移在此层校验，仓储只负责持久化。
class TaskService final {
public:
    explicit TaskService(std::shared_ptr<TaskRepository> repository);

    TaskResult<TaskPage> list(TaskSearchQuery query) const;
    TaskResult<Task> get(std::int64_t id) const;
    TaskResult<Task> create(TaskDraft draft) const;
    TaskResult<Task> update(
        std::int64_t id,
        std::uint32_t expectedRevision,
        TaskDraft draft
    ) const;
    // 推进任务状态；非法迁移返回 InvalidTransition。
    TaskResult<Task> setStatus(
        std::int64_t id,
        std::uint32_t expectedRevision,
        TaskStatus target
    ) const;
    // 工程师发布报价后回填关联报价。
    TaskResult<Task> setQuoteId(
        std::int64_t id,
        std::uint32_t expectedRevision,
        const std::optional<std::int64_t>& quoteId
    ) const;

private:
    static std::optional<TaskError> validatePage(TaskSearchQuery* query);
    static std::optional<TaskError> validateDraft(TaskDraft* draft);
    static bool isValidTransition(TaskStatus from, TaskStatus to);
    static TaskError mapRepositoryError(const RepositoryError& error);
    // 生成任务编号：TASK-yyyyMMddHHmmsszzz，唯一性由数据库约束兜底。
    static QString generateTaskNumber();

    std::shared_ptr<TaskRepository> repository_;
};

} // namespace manage::data
