#pragma once

#include "manage/data/catalog_models.h"
#include "manage/data/catalog_repository.h"

#include <cstdint>
#include <memory>

namespace manage::data {

class CatalogService final {
public:
    explicit CatalogService(std::shared_ptr<CatalogRepository> repository);

    CatalogResult<Page<Material>> listMaterials(PageQuery query) const;
    CatalogResult<Material> getMaterial(std::int64_t id) const;
    CatalogResult<Material> createMaterial(MaterialDraft draft) const;
    CatalogResult<Material> updateMaterial(
        std::int64_t id,
        std::uint32_t expectedRevision,
        MaterialDraft draft
    ) const;
    CatalogResult<Material> setMaterialEnabled(
        std::int64_t id,
        std::uint32_t expectedRevision,
        bool enabled
    ) const;

    CatalogResult<Page<Customer>> listCustomers(PageQuery query) const;
    CatalogResult<Customer> getCustomer(std::int64_t id) const;
    CatalogResult<Customer> createCustomer(CustomerDraft draft) const;
    CatalogResult<Customer> updateCustomer(
        std::int64_t id,
        std::uint32_t expectedRevision,
        CustomerDraft draft
    ) const;

private:
    static std::optional<CatalogError> validatePage(PageQuery* query);
    static std::optional<CatalogError> validateMaterial(MaterialDraft* draft);
    static std::optional<CatalogError> validateCustomer(CustomerDraft* draft);
    static CatalogError mapRepositoryError(const RepositoryError& error);

    std::shared_ptr<CatalogRepository> repository_;
};

} // namespace manage::data
