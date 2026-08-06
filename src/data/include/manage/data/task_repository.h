#pragma once

#include "manage/data/catalog_models.h"
#include "manage/data/task_models.h"

#include <QSqlDatabase>

#include <cstdint>

namespace manage::data {

class TaskRepository {
public:
    virtual ~TaskRepository() = default;

    virtual bool list(
        const TaskSearchQuery& query,
        TaskPage* page,
        RepositoryError* error
    ) = 0;
    virtual bool find(
        std::int64_t id,
        Task* task,
        RepositoryError* error
    ) = 0;
    virtual bool create(
        const TaskDraft& draft,
        Task* task,
        RepositoryError* error
    ) = 0;
    virtual bool update(
        std::int64_t id,
        std::uint32_t expectedRevision,
        const TaskDraft& draft,
        Task* task,
        RepositoryError* error
    ) = 0;
    virtual bool setStatus(
        std::int64_t id,
        std::uint32_t expectedRevision,
        TaskStatus status,
        Task* task,
        RepositoryError* error
    ) = 0;
    virtual bool setQuoteId(
        std::int64_t id,
        std::uint32_t expectedRevision,
        const std::optional<std::int64_t>& quoteId,
        Task* task,
        RepositoryError* error
    ) = 0;
};

class MySqlTaskRepository final : public TaskRepository {
public:
    explicit MySqlTaskRepository(QSqlDatabase database);

    bool list(
        const TaskSearchQuery& query,
        TaskPage* page,
        RepositoryError* error
    ) override;
    bool find(
        std::int64_t id,
        Task* task,
        RepositoryError* error
    ) override;
    bool create(
        const TaskDraft& draft,
        Task* task,
        RepositoryError* error
    ) override;
    bool update(
        std::int64_t id,
        std::uint32_t expectedRevision,
        const TaskDraft& draft,
        Task* task,
        RepositoryError* error
    ) override;
    bool setStatus(
        std::int64_t id,
        std::uint32_t expectedRevision,
        TaskStatus status,
        Task* task,
        RepositoryError* error
    ) override;
    bool setQuoteId(
        std::int64_t id,
        std::uint32_t expectedRevision,
        const std::optional<std::int64_t>& quoteId,
        Task* task,
        RepositoryError* error
    ) override;

private:
    static void clearError(RepositoryError* error);
    static void setError(
        RepositoryError* error,
        RepositoryErrorCode code,
        const QString& message
    );
    bool recordExists(std::int64_t id, bool* exists, RepositoryError* error);

    QSqlDatabase database_;
};

} // namespace manage::data
