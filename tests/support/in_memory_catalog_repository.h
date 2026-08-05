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

    std::int64_t nextMaterialId{1};
    std::int64_t nextCustomerId{1};
};
