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
    // 物料库三级向导的整包提交：物料 + 供应商 + 价格，服务端事务原子写入。
    CatalogResult<MaterialBundleResult> createMaterialBundle(
        MaterialBundleDraft bundle
    ) const;
    // 编辑模式：整包替换（乐观锁），事务原子更新物料 + 供应商 + 价格。
    CatalogResult<MaterialBundleResult> replaceMaterialBundle(
        std::int64_t materialId,
        std::uint32_t expectedRevision,
        MaterialBundleDraft bundle
    ) const;
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

    CatalogResult<Page<MaterialSupplier>> listMaterialSuppliers(
        std::int64_t materialId,
        PageQuery query
    ) const;
    CatalogResult<MaterialSupplier> getMaterialSupplier(std::int64_t id) const;
    CatalogResult<MaterialSupplier> createMaterialSupplier(
        std::int64_t materialId,
        MaterialSupplierDraft draft
    ) const;
    CatalogResult<MaterialSupplier> updateMaterialSupplier(
        std::int64_t id,
        std::uint32_t expectedRevision,
        MaterialSupplierDraft draft
    ) const;
    CatalogResult<MaterialSupplier> setMaterialSupplierEnabled(
        std::int64_t id,
        std::uint32_t expectedRevision,
        bool enabled
    ) const;

    CatalogResult<Page<MaterialPrice>> listMaterialPrices(
        std::int64_t supplierId,
        PageQuery query
    ) const;
    CatalogResult<MaterialPrice> getMaterialPrice(std::int64_t id) const;
    CatalogResult<MaterialPrice> createMaterialPrice(
        std::int64_t supplierId,
        MaterialPriceDraft draft
    ) const;
    CatalogResult<MaterialPrice> updateMaterialPrice(
        std::int64_t id,
        std::uint32_t expectedRevision,
        MaterialPriceDraft draft
    ) const;
    CatalogResult<MaterialPrice> setMaterialPriceEnabled(
        std::int64_t id,
        std::uint32_t expectedRevision,
        bool enabled
    ) const;

    // 批次9：按（物料, 供应商, 当前铜价）解析真实单价，供 BOM/报价编辑回填。
    CatalogResult<ResolvedMaterialPrice> resolveMaterialPrice(
        std::int64_t materialId,
        std::int64_t supplierId,
        std::optional<std::int64_t> copperPriceCents
    ) const;

private:
    static std::optional<CatalogError> validatePage(PageQuery* query);
    static std::optional<CatalogError> validateMaterial(MaterialDraft* draft);
    static std::optional<CatalogError> validateCustomer(CustomerDraft* draft);
    static std::optional<CatalogError> validateMaterialSupplier(MaterialSupplierDraft* draft);
    static std::optional<CatalogError> validateMaterialPrice(MaterialPriceDraft* draft);
    static CatalogError mapRepositoryError(const RepositoryError& error);

    std::shared_ptr<CatalogRepository> repository_;
};

} // namespace manage::data
