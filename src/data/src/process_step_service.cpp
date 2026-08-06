#include "manage/data/process_step_service.h"

#include <QRegularExpression>

#include <utility>

namespace manage::data {
namespace {

constexpr int kMaximumPage = 1'000'000;
// 单人工时上限：24 小时 * 60 分钟 = 1440 分钟。
constexpr int kMaximumLaborMinutes = 1'440;

template <typename T>
ProcessStepResult<T> failed(ProcessStepError error) {
    ProcessStepResult<T> result;
    result.error = std::move(error);
    return result;
}

template <typename T>
ProcessStepResult<T> succeeded(T value) {
    ProcessStepResult<T> result;
    result.value = std::move(value);
    return result;
}

ProcessStepError validationError(
    const QString& field,
    const QString& message
) {
    return {ProcessStepErrorCode::InvalidRequest, message, field};
}

QString trimmedOrEmpty(const QString& value) {
    return value.isNull() ? QStringLiteral("") : value.trimmed();
}

} // namespace

ProcessStepService::ProcessStepService(
    std::shared_ptr<ProcessStepRepository> repository
)
    : repository_(std::move(repository)) {}

std::optional<ProcessStepError> ProcessStepService::validatePage(
    PageQuery* query
) {
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

std::optional<ProcessStepError> ProcessStepService::validateDraft(
    ProcessStepDraft* draft
) {
    draft->code = draft->code.trimmed();
    draft->name = draft->name.trimmed();
    draft->description = trimmedOrEmpty(draft->description);

    static const QRegularExpression codePattern(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$")
    );
    if (!codePattern.match(draft->code).hasMatch()) {
        return validationError(
            QStringLiteral("code"),
            QStringLiteral(
                "code must be 1-64 ASCII letters, numbers, dot, underscore or hyphen"
            )
        );
    }
    if (draft->name.isEmpty() || draft->name.size() > 200) {
        return validationError(
            QStringLiteral("name"),
            QStringLiteral("name must contain 1-200 characters")
        );
    }
    if (draft->laborMinutes < 0 || draft->laborMinutes > kMaximumLaborMinutes) {
        return validationError(
            QStringLiteral("laborMinutes"),
            QStringLiteral(
                "laborMinutes must be between 0 and 1440 minutes per person"
            )
        );
    }
    if (draft->description.size() > 1000) {
        return validationError(
            QStringLiteral("description"),
            QStringLiteral("description must not exceed 1000 characters")
        );
    }
    return std::nullopt;
}

ProcessStepError ProcessStepService::mapRepositoryError(
    const RepositoryError& error
) {
    switch (error.code) {
    case RepositoryErrorCode::NotFound:
        return {ProcessStepErrorCode::NotFound, error.message, {}};
    case RepositoryErrorCode::RevisionConflict:
        return {ProcessStepErrorCode::RevisionConflict, error.message, {}};
    case RepositoryErrorCode::Duplicate:
        return {ProcessStepErrorCode::DuplicateCode, error.message, QStringLiteral("code")};
    case RepositoryErrorCode::None:
    case RepositoryErrorCode::Database:
        return {ProcessStepErrorCode::Database, error.message, {}};
    }
    return {ProcessStepErrorCode::Database, error.message, {}};
}

ProcessStepResult<ProcessStepPage> ProcessStepService::list(
    PageQuery query
) const {
    if (const auto error = validatePage(&query); error.has_value()) {
        return failed<ProcessStepPage>(*error);
    }
    ProcessStepPage page;
    RepositoryError repositoryError;
    if (!repository_->list(query, &page, &repositoryError)) {
        return failed<ProcessStepPage>(mapRepositoryError(repositoryError));
    }
    return succeeded(std::move(page));
}

ProcessStepResult<ProcessStep> ProcessStepService::get(std::int64_t id) const {
    if (id <= 0) {
        return failed<ProcessStep>(validationError(
            QStringLiteral("id"),
            QStringLiteral("id must be greater than zero")
        ));
    }
    ProcessStep step;
    RepositoryError repositoryError;
    if (!repository_->find(id, &step, &repositoryError)) {
        return failed<ProcessStep>(mapRepositoryError(repositoryError));
    }
    return succeeded(std::move(step));
}

ProcessStepResult<ProcessStep> ProcessStepService::create(
    ProcessStepDraft draft
) const {
    if (const auto error = validateDraft(&draft); error.has_value()) {
        return failed<ProcessStep>(*error);
    }
    ProcessStep step;
    RepositoryError repositoryError;
    if (!repository_->create(draft, &step, &repositoryError)) {
        return failed<ProcessStep>(mapRepositoryError(repositoryError));
    }
    return succeeded(std::move(step));
}

ProcessStepResult<ProcessStep> ProcessStepService::update(
    std::int64_t id,
    std::uint32_t expectedRevision,
    ProcessStepDraft draft
) const {
    if (id <= 0 || expectedRevision == 0) {
        return failed<ProcessStep>(validationError(
            id <= 0 ? QStringLiteral("id") : QStringLiteral("revision"),
            QStringLiteral("id and revision must be positive integers")
        ));
    }
    if (const auto error = validateDraft(&draft); error.has_value()) {
        return failed<ProcessStep>(*error);
    }
    ProcessStep step;
    RepositoryError repositoryError;
    if (!repository_->update(id, expectedRevision, draft, &step, &repositoryError)) {
        return failed<ProcessStep>(mapRepositoryError(repositoryError));
    }
    return succeeded(std::move(step));
}

ProcessStepResult<ProcessStep> ProcessStepService::setEnabled(
    std::int64_t id,
    std::uint32_t expectedRevision,
    bool enabled
) const {
    if (id <= 0 || expectedRevision == 0) {
        return failed<ProcessStep>(validationError(
            id <= 0 ? QStringLiteral("id") : QStringLiteral("revision"),
            QStringLiteral("id and revision must be positive integers")
        ));
    }
    ProcessStep step;
    RepositoryError repositoryError;
    if (!repository_->setEnabled(id, expectedRevision, enabled, &step, &repositoryError)) {
        return failed<ProcessStep>(mapRepositoryError(repositoryError));
    }
    return succeeded(std::move(step));
}

} // namespace manage::data
