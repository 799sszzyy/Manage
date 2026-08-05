#include "manage/data/catalog_service.h"

#include <QRegularExpression>

#include <utility>

namespace manage::data {
namespace {

constexpr int kMaximumPage = 1'000'000;

template <typename T>
CatalogResult<T> failed(CatalogError error) {
    CatalogResult<T> result;
    result.error = std::move(error);
    return result;
}

template <typename T>
CatalogResult<T> succeeded(T value) {
    CatalogResult<T> result;
    result.value = std::move(value);
    return result;
}

CatalogError validationError(
    const QString& field,
    const QString& message
) {
    return {CatalogErrorCode::InvalidRequest, message, field};
}

QString trimmedOrEmpty(const QString& value) {
    return value.isNull() ? QStringLiteral("") : value.trimmed();
}

} // namespace

CatalogService::CatalogService(std::shared_ptr<CatalogRepository> repository)
    : repository_(std::move(repository)) {}

std::optional<CatalogError> CatalogService::validatePage(PageQuery* query) {
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

std::optional<CatalogError> CatalogService::validateMaterial(MaterialDraft* draft) {
    draft->code = draft->code.trimmed();
    draft->name = draft->name.trimmed();
    draft->specification = trimmedOrEmpty(draft->specification);
    draft->unit = draft->unit.trimmed();
    draft->category = trimmedOrEmpty(draft->category);

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
    if (draft->specification.size() > 500) {
        return validationError(
            QStringLiteral("specification"),
            QStringLiteral("specification must not exceed 500 characters")
        );
    }
    if (draft->unit.isEmpty() || draft->unit.size() > 32) {
        return validationError(
            QStringLiteral("unit"),
            QStringLiteral("unit must contain 1-32 characters")
        );
    }
    if (draft->category.size() > 100) {
        return validationError(
            QStringLiteral("category"),
            QStringLiteral("category must not exceed 100 characters")
        );
    }
    if (draft->currentUnitPriceCents < 0) {
        return validationError(
            QStringLiteral("currentUnitPriceCents"),
            QStringLiteral("currentUnitPriceCents must not be negative")
        );
    }
    return std::nullopt;
}

std::optional<CatalogError> CatalogService::validateCustomer(CustomerDraft* draft) {
    draft->name = draft->name.trimmed();
    draft->contactName = trimmedOrEmpty(draft->contactName);
    draft->phone = trimmedOrEmpty(draft->phone);
    draft->address = trimmedOrEmpty(draft->address);
    draft->notes = trimmedOrEmpty(draft->notes);

    if (draft->name.isEmpty() || draft->name.size() > 200) {
        return validationError(
            QStringLiteral("name"),
            QStringLiteral("name must contain 1-200 characters")
        );
    }
    if (draft->contactName.size() > 100) {
        return validationError(
            QStringLiteral("contactName"),
            QStringLiteral("contactName must not exceed 100 characters")
        );
    }
    if (draft->phone.size() > 64) {
        return validationError(
            QStringLiteral("phone"),
            QStringLiteral("phone must not exceed 64 characters")
        );
    }
    if (draft->address.size() > 500) {
        return validationError(
            QStringLiteral("address"),
            QStringLiteral("address must not exceed 500 characters")
        );
    }
    if (draft->notes.size() > 10'000) {
        return validationError(
            QStringLiteral("notes"),
            QStringLiteral("notes must not exceed 10000 characters")
        );
    }
    return std::nullopt;
}

CatalogError CatalogService::mapRepositoryError(const RepositoryError& error) {
    switch (error.code) {
    case RepositoryErrorCode::NotFound:
        return {CatalogErrorCode::NotFound, error.message, {}};
    case RepositoryErrorCode::RevisionConflict:
        return {
            CatalogErrorCode::RevisionConflict,
            QStringLiteral("the record was changed by another request; reload and retry"),
            QStringLiteral("revision")
        };
    case RepositoryErrorCode::Duplicate:
        return {
            CatalogErrorCode::DuplicateCode,
            QStringLiteral("material code already exists"),
            QStringLiteral("code")
        };
    case RepositoryErrorCode::Database:
    case RepositoryErrorCode::None:
        return {
            CatalogErrorCode::Database,
            QStringLiteral("database operation failed"),
            {}
        };
    }
    return {CatalogErrorCode::Database, QStringLiteral("database operation failed"), {}};
}

CatalogResult<Page<Material>> CatalogService::listMaterials(PageQuery query) const {
    if (const auto error = validatePage(&query); error.has_value()) {
        return failed<Page<Material>>(*error);
    }
    Page<Material> page;
    RepositoryError repositoryError;
    if (!repository_->listMaterials(query, &page, &repositoryError)) {
        return failed<Page<Material>>(mapRepositoryError(repositoryError));
    }
    return succeeded(std::move(page));
}

CatalogResult<Material> CatalogService::getMaterial(std::int64_t id) const {
    if (id <= 0) {
        return failed<Material>(validationError(
            QStringLiteral("id"),
            QStringLiteral("id must be a positive integer")
        ));
    }
    Material material;
    RepositoryError repositoryError;
    if (!repository_->findMaterial(id, &material, &repositoryError)) {
        return failed<Material>(mapRepositoryError(repositoryError));
    }
    return succeeded(std::move(material));
}

CatalogResult<Material> CatalogService::createMaterial(MaterialDraft draft) const {
    if (const auto error = validateMaterial(&draft); error.has_value()) {
        return failed<Material>(*error);
    }
    Material material;
    RepositoryError repositoryError;
    if (!repository_->createMaterial(draft, &material, &repositoryError)) {
        return failed<Material>(mapRepositoryError(repositoryError));
    }
    return succeeded(std::move(material));
}

CatalogResult<Material> CatalogService::updateMaterial(
    std::int64_t id,
    std::uint32_t expectedRevision,
    MaterialDraft draft
) const {
    if (id <= 0) {
        return failed<Material>(validationError(
            QStringLiteral("id"),
            QStringLiteral("id must be a positive integer")
        ));
    }
    if (expectedRevision == 0) {
        return failed<Material>(validationError(
            QStringLiteral("revision"),
            QStringLiteral("revision must be a positive integer")
        ));
    }
    if (const auto error = validateMaterial(&draft); error.has_value()) {
        return failed<Material>(*error);
    }
    Material material;
    RepositoryError repositoryError;
    if (!repository_->updateMaterial(
            id,
            expectedRevision,
            draft,
            &material,
            &repositoryError
        )) {
        return failed<Material>(mapRepositoryError(repositoryError));
    }
    return succeeded(std::move(material));
}

CatalogResult<Material> CatalogService::setMaterialEnabled(
    std::int64_t id,
    std::uint32_t expectedRevision,
    bool enabled
) const {
    if (id <= 0 || expectedRevision == 0) {
        return failed<Material>(validationError(
            id <= 0 ? QStringLiteral("id") : QStringLiteral("revision"),
            QStringLiteral("id and revision must be positive integers")
        ));
    }
    Material material;
    RepositoryError repositoryError;
    if (!repository_->setMaterialEnabled(
            id,
            expectedRevision,
            enabled,
            &material,
            &repositoryError
        )) {
        return failed<Material>(mapRepositoryError(repositoryError));
    }
    return succeeded(std::move(material));
}

CatalogResult<Page<Customer>> CatalogService::listCustomers(PageQuery query) const {
    query.enabled.reset();
    if (const auto error = validatePage(&query); error.has_value()) {
        return failed<Page<Customer>>(*error);
    }
    Page<Customer> page;
    RepositoryError repositoryError;
    if (!repository_->listCustomers(query, &page, &repositoryError)) {
        return failed<Page<Customer>>(mapRepositoryError(repositoryError));
    }
    return succeeded(std::move(page));
}

CatalogResult<Customer> CatalogService::getCustomer(std::int64_t id) const {
    if (id <= 0) {
        return failed<Customer>(validationError(
            QStringLiteral("id"),
            QStringLiteral("id must be a positive integer")
        ));
    }
    Customer customer;
    RepositoryError repositoryError;
    if (!repository_->findCustomer(id, &customer, &repositoryError)) {
        return failed<Customer>(mapRepositoryError(repositoryError));
    }
    return succeeded(std::move(customer));
}

CatalogResult<Customer> CatalogService::createCustomer(CustomerDraft draft) const {
    if (const auto error = validateCustomer(&draft); error.has_value()) {
        return failed<Customer>(*error);
    }
    Customer customer;
    RepositoryError repositoryError;
    if (!repository_->createCustomer(draft, &customer, &repositoryError)) {
        return failed<Customer>(mapRepositoryError(repositoryError));
    }
    return succeeded(std::move(customer));
}

CatalogResult<Customer> CatalogService::updateCustomer(
    std::int64_t id,
    std::uint32_t expectedRevision,
    CustomerDraft draft
) const {
    if (id <= 0 || expectedRevision == 0) {
        return failed<Customer>(validationError(
            id <= 0 ? QStringLiteral("id") : QStringLiteral("revision"),
            QStringLiteral("id and revision must be positive integers")
        ));
    }
    if (const auto error = validateCustomer(&draft); error.has_value()) {
        return failed<Customer>(*error);
    }
    Customer customer;
    RepositoryError repositoryError;
    if (!repository_->updateCustomer(
            id,
            expectedRevision,
            draft,
            &customer,
            &repositoryError
        )) {
        return failed<Customer>(mapRepositoryError(repositoryError));
    }
    return succeeded(std::move(customer));
}

} // namespace manage::data
