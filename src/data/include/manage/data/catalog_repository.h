#pragma once

#include "manage/data/catalog_models.h"

#include <QSqlDatabase>

#include <cstdint>

namespace manage::data {

class CatalogRepository {
public:
    virtual ~CatalogRepository() = default;

    virtual bool listMaterials(
        const PageQuery& query,
        Page<Material>* page,
        RepositoryError* error
    ) = 0;
    virtual bool findMaterial(
        std::int64_t id,
        Material* material,
        RepositoryError* error
    ) = 0;
    virtual bool createMaterial(
        const MaterialDraft& draft,
        Material* material,
        RepositoryError* error
    ) = 0;
    // 整包原子创建：物料 + 供应商 + 价格在单个事务内写入。
    virtual bool createMaterialBundle(
        const MaterialBundleDraft& bundle,
        MaterialBundleResult* result,
        RepositoryError* error
    ) = 0;
    virtual bool updateMaterial(
        std::int64_t id,
        std::uint32_t expectedRevision,
        const MaterialDraft& draft,
        Material* material,
        RepositoryError* error
    ) = 0;
    virtual bool setMaterialEnabled(
        std::int64_t id,
        std::uint32_t expectedRevision,
        bool enabled,
        Material* material,
        RepositoryError* error
    ) = 0;

    // 物料供应商分支
    virtual bool listMaterialSuppliers(
        std::int64_t materialId,
        const PageQuery& query,
        Page<MaterialSupplier>* page,
        RepositoryError* error
    ) = 0;
    virtual bool findMaterialSupplier(
        std::int64_t id,
        MaterialSupplier* supplier,
        RepositoryError* error
    ) = 0;
    virtual bool createMaterialSupplier(
        std::int64_t materialId,
        const MaterialSupplierDraft& draft,
        MaterialSupplier* supplier,
        RepositoryError* error
    ) = 0;
    virtual bool updateMaterialSupplier(
        std::int64_t id,
        std::uint32_t expectedRevision,
        const MaterialSupplierDraft& draft,
        MaterialSupplier* supplier,
        RepositoryError* error
    ) = 0;
    virtual bool setMaterialSupplierEnabled(
        std::int64_t id,
        std::uint32_t expectedRevision,
        bool enabled,
        MaterialSupplier* supplier,
        RepositoryError* error
    ) = 0;

    // 供应商价格分支（电线类按铜价区分）
    virtual bool listMaterialPrices(
        std::int64_t supplierId,
        const PageQuery& query,
        Page<MaterialPrice>* page,
        RepositoryError* error
    ) = 0;
    virtual bool findMaterialPrice(
        std::int64_t id,
        MaterialPrice* price,
        RepositoryError* error
    ) = 0;
    virtual bool createMaterialPrice(
        std::int64_t supplierId,
        const MaterialPriceDraft& draft,
        MaterialPrice* price,
        RepositoryError* error
    ) = 0;
    virtual bool updateMaterialPrice(
        std::int64_t id,
        std::uint32_t expectedRevision,
        const MaterialPriceDraft& draft,
        MaterialPrice* price,
        RepositoryError* error
    ) = 0;
    virtual bool setMaterialPriceEnabled(
        std::int64_t id,
        std::uint32_t expectedRevision,
        bool enabled,
        MaterialPrice* price,
        RepositoryError* error
    ) = 0;

    virtual bool listCustomers(
        const PageQuery& query,
        Page<Customer>* page,
        RepositoryError* error
    ) = 0;
    virtual bool findCustomer(
        std::int64_t id,
        Customer* customer,
        RepositoryError* error
    ) = 0;
    virtual bool createCustomer(
        const CustomerDraft& draft,
        Customer* customer,
        RepositoryError* error
    ) = 0;
    virtual bool updateCustomer(
        std::int64_t id,
        std::uint32_t expectedRevision,
        const CustomerDraft& draft,
        Customer* customer,
        RepositoryError* error
    ) = 0;
};

class MySqlCatalogRepository final : public CatalogRepository {
public:
    explicit MySqlCatalogRepository(QSqlDatabase database);

    bool listMaterials(
        const PageQuery& query,
        Page<Material>* page,
        RepositoryError* error
    ) override;
    bool findMaterial(
        std::int64_t id,
        Material* material,
        RepositoryError* error
    ) override;
    bool createMaterial(
        const MaterialDraft& draft,
        Material* material,
        RepositoryError* error
    ) override;
    bool createMaterialBundle(
        const MaterialBundleDraft& bundle,
        MaterialBundleResult* result,
        RepositoryError* error
    ) override;
    bool updateMaterial(
        std::int64_t id,
        std::uint32_t expectedRevision,
        const MaterialDraft& draft,
        Material* material,
        RepositoryError* error
    ) override;
    bool setMaterialEnabled(
        std::int64_t id,
        std::uint32_t expectedRevision,
        bool enabled,
        Material* material,
        RepositoryError* error
    ) override;

    bool listMaterialSuppliers(
        std::int64_t materialId,
        const PageQuery& query,
        Page<MaterialSupplier>* page,
        RepositoryError* error
    ) override;
    bool findMaterialSupplier(
        std::int64_t id,
        MaterialSupplier* supplier,
        RepositoryError* error
    ) override;
    bool createMaterialSupplier(
        std::int64_t materialId,
        const MaterialSupplierDraft& draft,
        MaterialSupplier* supplier,
        RepositoryError* error
    ) override;
    bool updateMaterialSupplier(
        std::int64_t id,
        std::uint32_t expectedRevision,
        const MaterialSupplierDraft& draft,
        MaterialSupplier* supplier,
        RepositoryError* error
    ) override;
    bool setMaterialSupplierEnabled(
        std::int64_t id,
        std::uint32_t expectedRevision,
        bool enabled,
        MaterialSupplier* supplier,
        RepositoryError* error
    ) override;

    bool listMaterialPrices(
        std::int64_t supplierId,
        const PageQuery& query,
        Page<MaterialPrice>* page,
        RepositoryError* error
    ) override;
    bool findMaterialPrice(
        std::int64_t id,
        MaterialPrice* price,
        RepositoryError* error
    ) override;
    bool createMaterialPrice(
        std::int64_t supplierId,
        const MaterialPriceDraft& draft,
        MaterialPrice* price,
        RepositoryError* error
    ) override;
    bool updateMaterialPrice(
        std::int64_t id,
        std::uint32_t expectedRevision,
        const MaterialPriceDraft& draft,
        MaterialPrice* price,
        RepositoryError* error
    ) override;
    bool setMaterialPriceEnabled(
        std::int64_t id,
        std::uint32_t expectedRevision,
        bool enabled,
        MaterialPrice* price,
        RepositoryError* error
    ) override;

    bool listCustomers(
        const PageQuery& query,
        Page<Customer>* page,
        RepositoryError* error
    ) override;
    bool findCustomer(
        std::int64_t id,
        Customer* customer,
        RepositoryError* error
    ) override;
    bool createCustomer(
        const CustomerDraft& draft,
        Customer* customer,
        RepositoryError* error
    ) override;
    bool updateCustomer(
        std::int64_t id,
        std::uint32_t expectedRevision,
        const CustomerDraft& draft,
        Customer* customer,
        RepositoryError* error
    ) override;

private:
    bool recordExists(const QString& table, std::int64_t id, bool* exists, RepositoryError* error);
    static void clearError(RepositoryError* error);
    static void setError(
        RepositoryError* error,
        RepositoryErrorCode code,
        const QString& message
    );

    QSqlDatabase database_;
};

} // namespace manage::data
