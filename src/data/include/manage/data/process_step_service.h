#pragma once

#include "manage/data/process_step_models.h"
#include "manage/data/process_step_repository.h"

#include <cstdint>
#include <memory>

namespace manage::data {

class ProcessStepService final {
public:
    explicit ProcessStepService(std::shared_ptr<ProcessStepRepository> repository);

    ProcessStepResult<ProcessStepPage> list(PageQuery query) const;
    ProcessStepResult<ProcessStep> get(std::int64_t id) const;
    ProcessStepResult<ProcessStep> create(ProcessStepDraft draft) const;
    ProcessStepResult<ProcessStep> update(
        std::int64_t id,
        std::uint32_t expectedRevision,
        ProcessStepDraft draft
    ) const;
    ProcessStepResult<ProcessStep> setEnabled(
        std::int64_t id,
        std::uint32_t expectedRevision,
        bool enabled
    ) const;

private:
    static std::optional<ProcessStepError> validatePage(PageQuery* query);
    static std::optional<ProcessStepError> validateDraft(ProcessStepDraft* draft);
    static ProcessStepError mapRepositoryError(const RepositoryError& error);

    std::shared_ptr<ProcessStepRepository> repository_;
};

} // namespace manage::data
