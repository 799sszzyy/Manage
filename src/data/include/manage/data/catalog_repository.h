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
