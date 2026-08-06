#pragma once

#include "manage/data/catalog_models.h"
#include "manage/data/process_step_models.h"

#include <QSqlDatabase>

#include <cstdint>

namespace manage::data {

class ProcessStepRepository {
public:
    virtual ~ProcessStepRepository() = default;

    virtual bool list(
        const PageQuery& query,
        ProcessStepPage* page,
        RepositoryError* error
    ) = 0;
    virtual bool find(
        std::int64_t id,
        ProcessStep* step,
        RepositoryError* error
    ) = 0;
    virtual bool create(
        const ProcessStepDraft& draft,
        ProcessStep* step,
        RepositoryError* error
    ) = 0;
    virtual bool update(
        std::int64_t id,
        std::uint32_t expectedRevision,
        const ProcessStepDraft& draft,
        ProcessStep* step,
        RepositoryError* error
    ) = 0;
    virtual bool setEnabled(
        std::int64_t id,
        std::uint32_t expectedRevision,
        bool enabled,
        ProcessStep* step,
        RepositoryError* error
    ) = 0;
};

class MySqlProcessStepRepository final : public ProcessStepRepository {
public:
    explicit MySqlProcessStepRepository(QSqlDatabase database);

    bool list(
        const PageQuery& query,
        ProcessStepPage* page,
        RepositoryError* error
    ) override;
    bool find(
        std::int64_t id,
        ProcessStep* step,
        RepositoryError* error
    ) override;
    bool create(
        const ProcessStepDraft& draft,
        ProcessStep* step,
        RepositoryError* error
    ) override;
    bool update(
        std::int64_t id,
        std::uint32_t expectedRevision,
        const ProcessStepDraft& draft,
        ProcessStep* step,
        RepositoryError* error
    ) override;
    bool setEnabled(
        std::int64_t id,
        std::uint32_t expectedRevision,
        bool enabled,
        ProcessStep* step,
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
