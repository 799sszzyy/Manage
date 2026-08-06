#pragma once

#include "manage/data/catalog_repository.h"

#include <algorithm>
#include <cstdint>
#include <vector>

class InMemoryCatalogRepository final : public manage::data::CatalogRepository {
public:
    bool listMaterials(
        const manage::data::PageQuery& query,
        manage::data::Page<manage::data::Material>* page,
        manage::data::RepositoryError* error
    ) override {
        clear(error);
        std::vector<manage::data::Material> filtered;
        for (const auto& material : materials) {
            const bool searchMatches = query.search.isEmpty() ||
                material.code.contains(query.search, Qt::CaseInsensitive) ||
                material.name.contains(query.search, Qt::CaseInsensitive) ||
                material.category.contains(query.search, Qt::CaseInsensitive);
            const bool enabledMatches = !query.enabled.has_value() ||
                material.isEnabled == *query.enabled;
            if (searchMatches && enabledMatches) {
                filtered.push_back(material);
            }
        }
        page->total = static_cast<std::int64_t>(filtered.size());
        page->page = query.page;
        page->pageSize = query.pageSize;
        const auto rawBegin =
            (static_cast<std::int64_t>(query.page) - 1) * query.pageSize;
        const auto begin = std::min<std::size_t>(
            static_cast<std::size_t>(rawBegin),
            filtered.size()
        );
        const auto end = std::min<std::size_t>(
            begin + static_cast<std::size_t>(query.pageSize),
            filtered.size()
        );
        page->items.assign(filtered.begin() + static_cast<std::ptrdiff_t>(begin),
                           filtered.begin() + static_cast<std::ptrdiff_t>(end));
        return true;
    }

    bool findMaterial(
        std::int64_t id,
        manage::data::Material* material,
        manage::data::RepositoryError* error
    ) override {
        clear(error);
        const auto found = std::find_if(materials.begin(), materials.end(),
                                        [id](const auto& item) { return item.id == id; });
        if (found == materials.end()) {
            fail(error, manage::data::RepositoryErrorCode::NotFound, QStringLiteral("material not found"));
            return false;
        }
        *material = *found;
        return true;
    }

    bool createMaterial(
        const manage::data::MaterialDraft& draft,
        manage::data::Material* material,
        manage::data::RepositoryError* error
    ) override {
        clear(error);
        const auto duplicate = std::find_if(materials.begin(), materials.end(),
                                             [&](const auto& item) { return item.code == draft.code; });
        if (duplicate != materials.end()) {
            fail(error, manage::data::RepositoryErrorCode::Duplicate, QStringLiteral("duplicate"));
            return false;
        }
        manage::data::Material created;
        created.id = nextMaterialId++;
        apply(draft, &created);
        created.revision = 1;
        created.createdAt = QDateTime::currentDateTimeUtc();
        created.updatedAt = created.createdAt;
        materials.push_back(created);
        *material = created;
        return true;
    }

    bool updateMaterial(
        std::int64_t id,
        std::uint32_t expectedRevision,
        const manage::data::MaterialDraft& draft,
        manage::data::Material* material,
        manage::data::RepositoryError* error
    ) override {
        clear(error);
        const auto found = std::find_if(materials.begin(), materials.end(),
                                        [id](const auto& item) { return item.id == id; });
        if (found == materials.end()) {
            fail(error, manage::data::RepositoryErrorCode::NotFound, QStringLiteral("material not found"));
            return false;
        }
        if (found->revision != expectedRevision) {
            fail(error, manage::data::RepositoryErrorCode::RevisionConflict, QStringLiteral("conflict"));
            return false;
        }
        const auto duplicate = std::find_if(materials.begin(), materials.end(),
            [&](const auto& item) { return item.id != id && item.code == draft.code; });
        if (duplicate != materials.end()) {
            fail(error, manage::data::RepositoryErrorCode::Duplicate, QStringLiteral("duplicate"));
            return false;
        }
        apply(draft, &*found);
        ++found->revision;
        found->updatedAt = QDateTime::currentDateTimeUtc();
        *material = *found;
        return true;
    }

    bool setMaterialEnabled(
        std::int64_t id,
        std::uint32_t expectedRevision,
        bool enabled,
        manage::data::Material* material,
        manage::data::RepositoryError* error
    ) override {
        clear(error);
        const auto found = std::find_if(materials.begin(), materials.end(),
                                        [id](const auto& item) { return item.id == id; });
        if (found == materials.end()) {
            fail(error, manage::data::RepositoryErrorCode::NotFound, QStringLiteral("material not found"));
            return false;
        }
        if (found->revision != expectedRevision) {
            fail(error, manage::data::RepositoryErrorCode::RevisionConflict, QStringLiteral("conflict"));
            return false;
        }
        found->isEnabled = enabled;
        ++found->revision;
        found->updatedAt = QDateTime::currentDateTimeUtc();
        *material = *found;
        return true;
    }

    bool listMaterialSuppliers(
        std::int64_t materialId,
        const manage::data::PageQuery& query,
        manage::data::Page<manage::data::MaterialSupplier>* page,
        manage::data::RepositoryError* error
    ) override {
        clear(error);
        std::vector<manage::data::MaterialSupplier> filtered;
        for (const auto& supplier : suppliers) {
            if (supplier.materialId != materialId) {
                continue;
            }
            if (!query.search.isEmpty() &&
                !supplier.supplierName.contains(query.search, Qt::CaseInsensitive) &&
                !supplier.contactName.contains(query.search, Qt::CaseInsensitive)) {
                continue;
            }
            filtered.push_back(supplier);
        }
        std::sort(filtered.begin(), filtered.end(), [](const auto& left, const auto& right) {
            if (left.isDefault != right.isDefault) {
                return left.isDefault;
            }
            return left.id < right.id;
        });
        page->total = static_cast<std::int64_t>(filtered.size());
        page->page = query.page;
        page->pageSize = query.pageSize;
        const auto rawBegin =
            (static_cast<std::int64_t>(query.page) - 1) * query.pageSize;
        const auto begin = std::min<std::size_t>(
            static_cast<std::size_t>(rawBegin), filtered.size());
        const auto end = std::min<std::size_t>(
            begin + static_cast<std::size_t>(query.pageSize), filtered.size());
        page->items.assign(filtered.begin() + static_cast<std::ptrdiff_t>(begin),
                           filtered.begin() + static_cast<std::ptrdiff_t>(end));
        return true;
    }

    bool findMaterialSupplier(
        std::int64_t id,
        manage::data::MaterialSupplier* supplier,
        manage::data::RepositoryError* error
    ) override {
        clear(error);
        const auto found = std::find_if(suppliers.begin(), suppliers.end(),
                                        [id](const auto& item) { return item.id == id; });
        if (found == suppliers.end()) {
            fail(error, manage::data::RepositoryErrorCode::NotFound, QStringLiteral("supplier not found"));
            return false;
        }
        *supplier = *found;
        return true;
    }

    bool createMaterialSupplier(
        std::int64_t materialId,
        const manage::data::MaterialSupplierDraft& draft,
        manage::data::MaterialSupplier* supplier,
        manage::data::RepositoryError* error
    ) override {
        clear(error);
        const auto materialFound = std::find_if(materials.begin(), materials.end(),
                                                [materialId](const auto& item) { return item.id == materialId; });
        if (materialFound == materials.end()) {
            fail(error, manage::data::RepositoryErrorCode::NotFound, QStringLiteral("material not found"));
            return false;
        }
        const auto duplicate = std::find_if(suppliers.begin(), suppliers.end(),
            [&](const auto& item) {
                return item.materialId == materialId && item.supplierName == draft.supplierName;
            });
        if (duplicate != suppliers.end()) {
            fail(error, manage::data::RepositoryErrorCode::Duplicate, QStringLiteral("duplicate supplier"));
            return false;
        }
        manage::data::MaterialSupplier created;
        created.id = nextSupplierId++;
        created.materialId = materialId;
        apply(draft, &created);
        created.revision = 1;
        created.createdAt = QDateTime::currentDateTimeUtc();
        created.updatedAt = created.createdAt;
        suppliers.push_back(created);
        *supplier = created;
        return true;
    }

    bool updateMaterialSupplier(
        std::int64_t id,
        std::uint32_t expectedRevision,
        const manage::data::MaterialSupplierDraft& draft,
        manage::data::MaterialSupplier* supplier,
        manage::data::RepositoryError* error
    ) override {
        clear(error);
        const auto found = std::find_if(suppliers.begin(), suppliers.end(),
                                        [id](const auto& item) { return item.id == id; });
        if (found == suppliers.end()) {
            fail(error, manage::data::RepositoryErrorCode::NotFound, QStringLiteral("supplier not found"));
            return false;
        }
        if (found->revision != expectedRevision) {
            fail(error, manage::data::RepositoryErrorCode::RevisionConflict, QStringLiteral("conflict"));
            return false;
        }
        const auto duplicate = std::find_if(suppliers.begin(), suppliers.end(),
            [&](const auto& item) {
                return item.id != id && item.materialId == found->materialId &&
                       item.supplierName == draft.supplierName;
            });
        if (duplicate != suppliers.end()) {
            fail(error, manage::data::RepositoryErrorCode::Duplicate, QStringLiteral("duplicate supplier"));
            return false;
        }
        apply(draft, &*found);
        ++found->revision;
        found->updatedAt = QDateTime::currentDateTimeUtc();
        *supplier = *found;
        return true;
    }

    bool setMaterialSupplierEnabled(
        std::int64_t id,
        std::uint32_t expectedRevision,
        bool enabled,
        manage::data::MaterialSupplier* supplier,
        manage::data::RepositoryError* error
    ) override {
        clear(error);
        const auto found = std::find_if(suppliers.begin(), suppliers.end(),
                                        [id](const auto& item) { return item.id == id; });
        if (found == suppliers.end()) {
            fail(error, manage::data::RepositoryErrorCode::NotFound, QStringLiteral("supplier not found"));
            return false;
        }
        if (found->revision != expectedRevision) {
            fail(error, manage::data::RepositoryErrorCode::RevisionConflict, QStringLiteral("conflict"));
            return false;
        }
        found->isEnabled = enabled;
        ++found->revision;
        found->updatedAt = QDateTime::currentDateTimeUtc();
        *supplier = *found;
        return true;
    }

    bool listMaterialPrices(
        std::int64_t supplierId,
        const manage::data::PageQuery& query,
        manage::data::Page<manage::data::MaterialPrice>* page,
        manage::data::RepositoryError* error
    ) override {
        clear(error);
        std::vector<manage::data::MaterialPrice> filtered;
        for (const auto& price : prices) {
            if (price.supplierId == supplierId) {
                filtered.push_back(price);
            }
        }
        std::sort(filtered.begin(), filtered.end(), [](const auto& left, const auto& right) {
            if (left.copperPriceCents.has_value() != right.copperPriceCents.has_value()) {
                return left.copperPriceCents.has_value();
            }
            if (left.copperPriceCents.has_value() &&
                *left.copperPriceCents != *right.copperPriceCents) {
                return *left.copperPriceCents < *right.copperPriceCents;
            }
            if (left.isDefault != right.isDefault) {
                return left.isDefault;
            }
            return left.id < right.id;
        });
        page->total = static_cast<std::int64_t>(filtered.size());
        page->page = query.page;
        page->pageSize = query.pageSize;
        const auto rawBegin =
            (static_cast<std::int64_t>(query.page) - 1) * query.pageSize;
        const auto begin = std::min<std::size_t>(
            static_cast<std::size_t>(rawBegin), filtered.size());
        const auto end = std::min<std::size_t>(
            begin + static_cast<std::size_t>(query.pageSize), filtered.size());
        page->items.assign(filtered.begin() + static_cast<std::ptrdiff_t>(begin),
                           filtered.begin() + static_cast<std::ptrdiff_t>(end));
        return true;
    }

    bool findMaterialPrice(
        std::int64_t id,
        manage::data::MaterialPrice* price,
        manage::data::RepositoryError* error
    ) override {
        clear(error);
        const auto found = std::find_if(prices.begin(), prices.end(),
                                        [id](const auto& item) { return item.id == id; });
        if (found == prices.end()) {
            fail(error, manage::data::RepositoryErrorCode::NotFound, QStringLiteral("price not found"));
            return false;
        }
        *price = *found;
        return true;
    }

    bool createMaterialPrice(
        std::int64_t supplierId,
        const manage::data::MaterialPriceDraft& draft,
        manage::data::MaterialPrice* price,
        manage::data::RepositoryError* error
    ) override {
        clear(error);
        const auto supplierFound = std::find_if(suppliers.begin(), suppliers.end(),
                                                [supplierId](const auto& item) { return item.id == supplierId; });
        if (supplierFound == suppliers.end()) {
            fail(error, manage::data::RepositoryErrorCode::NotFound, QStringLiteral("supplier not found"));
            return false;
        }
        const auto duplicate = std::find_if(prices.begin(), prices.end(),
            [&](const auto& item) {
                return item.supplierId == supplierId &&
                       item.copperPriceCents == draft.copperPriceCents;
            });
        if (duplicate != prices.end()) {
            fail(error, manage::data::RepositoryErrorCode::Duplicate, QStringLiteral("duplicate price"));
            return false;
        }
        manage::data::MaterialPrice created;
        created.id = nextPriceId++;
        created.supplierId = supplierId;
        apply(draft, &created);
        created.revision = 1;
        created.createdAt = QDateTime::currentDateTimeUtc();
        created.updatedAt = created.createdAt;
        prices.push_back(created);
        *price = created;
        return true;
    }

    bool updateMaterialPrice(
        std::int64_t id,
        std::uint32_t expectedRevision,
        const manage::data::MaterialPriceDraft& draft,
        manage::data::MaterialPrice* price,
        manage::data::RepositoryError* error
    ) override {
        clear(error);
        const auto found = std::find_if(prices.begin(), prices.end(),
                                        [id](const auto& item) { return item.id == id; });
        if (found == prices.end()) {
            fail(error, manage::data::RepositoryErrorCode::NotFound, QStringLiteral("price not found"));
            return false;
        }
        if (found->revision != expectedRevision) {
            fail(error, manage::data::RepositoryErrorCode::RevisionConflict, QStringLiteral("conflict"));
            return false;
        }
        const auto duplicate = std::find_if(prices.begin(), prices.end(),
            [&](const auto& item) {
                return item.id != id && item.supplierId == found->supplierId &&
                       item.copperPriceCents == draft.copperPriceCents;
            });
        if (duplicate != prices.end()) {
            fail(error, manage::data::RepositoryErrorCode::Duplicate, QStringLiteral("duplicate price"));
            return false;
        }
        apply(draft, &*found);
        ++found->revision;
        found->updatedAt = QDateTime::currentDateTimeUtc();
        *price = *found;
        return true;
    }

    bool setMaterialPriceEnabled(
        std::int64_t id,
        std::uint32_t expectedRevision,
        bool enabled,
        manage::data::MaterialPrice* price,
        manage::data::RepositoryError* error
    ) override {
        clear(error);
        const auto found = std::find_if(prices.begin(), prices.end(),
                                        [id](const auto& item) { return item.id == id; });
        if (found == prices.end()) {
            fail(error, manage::data::RepositoryErrorCode::NotFound, QStringLiteral("price not found"));
            return false;
        }
        if (found->revision != expectedRevision) {
            fail(error, manage::data::RepositoryErrorCode::RevisionConflict, QStringLiteral("conflict"));
            return false;
        }
        found->isEnabled = enabled;
        ++found->revision;
        found->updatedAt = QDateTime::currentDateTimeUtc();
        *price = *found;
        return true;
    }

    bool listCustomers(
        const manage::data::PageQuery& query,
        manage::data::Page<manage::data::Customer>* page,
        manage::data::RepositoryError* error
    ) override {
        clear(error);
        std::vector<manage::data::Customer> filtered;
        for (const auto& customer : customers) {
            if (query.search.isEmpty() ||
                customer.name.contains(query.search, Qt::CaseInsensitive) ||
                customer.contactName.contains(query.search, Qt::CaseInsensitive) ||
                customer.phone.contains(query.search, Qt::CaseInsensitive)) {
                filtered.push_back(customer);
            }
        }
        page->total = static_cast<std::int64_t>(filtered.size());
        page->page = query.page;
        page->pageSize = query.pageSize;
        const auto rawBegin =
            (static_cast<std::int64_t>(query.page) - 1) * query.pageSize;
        const auto begin = std::min<std::size_t>(
            static_cast<std::size_t>(rawBegin), filtered.size());
        const auto end = std::min<std::size_t>(
            begin + static_cast<std::size_t>(query.pageSize), filtered.size());
        page->items.assign(filtered.begin() + static_cast<std::ptrdiff_t>(begin),
                           filtered.begin() + static_cast<std::ptrdiff_t>(end));
        return true;
    }

    bool findCustomer(
        std::int64_t id,
        manage::data::Customer* customer,
        manage::data::RepositoryError* error
    ) override {
        clear(error);
        const auto found = std::find_if(customers.begin(), customers.end(),
                                        [id](const auto& item) { return item.id == id; });
        if (found == customers.end()) {
            fail(error, manage::data::RepositoryErrorCode::NotFound, QStringLiteral("customer not found"));
            return false;
        }
        *customer = *found;
        return true;
    }

    bool createCustomer(
        const manage::data::CustomerDraft& draft,
        manage::data::Customer* customer,
        manage::data::RepositoryError* error
    ) override {
        clear(error);
        manage::data::Customer created;
        created.id = nextCustomerId++;
        apply(draft, &created);
        created.revision = 1;
        created.createdAt = QDateTime::currentDateTimeUtc();
        created.updatedAt = created.createdAt;
        customers.push_back(created);
        *customer = created;
        return true;
    }

    bool updateCustomer(
        std::int64_t id,
        std::uint32_t expectedRevision,
        const manage::data::CustomerDraft& draft,
        manage::data::Customer* customer,
        manage::data::RepositoryError* error
    ) override {
        clear(error);
        const auto found = std::find_if(customers.begin(), customers.end(),
                                        [id](const auto& item) { return item.id == id; });
        if (found == customers.end()) {
            fail(error, manage::data::RepositoryErrorCode::NotFound, QStringLiteral("customer not found"));
            return false;
        }
        if (found->revision != expectedRevision) {
            fail(error, manage::data::RepositoryErrorCode::RevisionConflict, QStringLiteral("conflict"));
            return false;
        }
        apply(draft, &*found);
        ++found->revision;
        found->updatedAt = QDateTime::currentDateTimeUtc();
        *customer = *found;
        return true;
    }

    std::vector<manage::data::Material> materials;
    std::vector<manage::data::Customer> customers;
    std::vector<manage::data::MaterialSupplier> suppliers;
    std::vector<manage::data::MaterialPrice> prices;

private:
    static void clear(manage::data::RepositoryError* error) {
        if (error != nullptr) {
            *error = {};
        }
    }
    static void fail(
        manage::data::RepositoryError* error,
        manage::data::RepositoryErrorCode code,
        const QString& message
    ) {
        if (error != nullptr) {
            error->code = code;
            error->message = message;
        }
    }
    static void apply(
        const manage::data::MaterialDraft& draft,
        manage::data::Material* material
    ) {
        material->code = draft.code;
        material->name = draft.name;
        material->specification = draft.specification;
        material->unit = draft.unit;
        material->category = draft.category;
        material->isCopperBased = draft.isCopperBased;
        material->currentUnitPriceCents = draft.currentUnitPriceCents;
        material->isEnabled = draft.isEnabled;
    }
    static void apply(
        const manage::data::CustomerDraft& draft,
        manage::data::Customer* customer
    ) {
        customer->name = draft.name;
        customer->contactName = draft.contactName;
        customer->phone = draft.phone;
        customer->address = draft.address;
        customer->notes = draft.notes;
    }
    static void apply(
        const manage::data::MaterialSupplierDraft& draft,
        manage::data::MaterialSupplier* supplier
    ) {
        supplier->supplierName = draft.supplierName;
        supplier->contactName = draft.contactName;
        supplier->phone = draft.phone;
        supplier->isDefault = draft.isDefault;
        supplier->isEnabled = draft.isEnabled;
    }
    static void apply(
        const manage::data::MaterialPriceDraft& draft,
        manage::data::MaterialPrice* price
    ) {
        price->copperPriceCents = draft.copperPriceCents;
        price->unitPriceCents = draft.unitPriceCents;
        price->isDefault = draft.isDefault;
        price->isEnabled = draft.isEnabled;
    }

    std::int64_t nextMaterialId{1};
    std::int64_t nextCustomerId{1};
    std::int64_t nextSupplierId{1};
    std::int64_t nextPriceId{1};
};
