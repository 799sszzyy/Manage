#include "manage/data/catalog_repository.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <utility>

namespace manage::data {
namespace {

constexpr auto kMaterialColumns =
    "id, code, name, specification, unit, category, "
    "current_unit_price_cents, is_enabled, revision, created_at, updated_at";
constexpr auto kCustomerColumns =
    "id, name, contact_name, phone, address, notes, revision, created_at, updated_at";

Material readMaterial(const QSqlQuery& query) {
    Material material;
    material.id = query.value(0).toLongLong();
    material.code = query.value(1).toString();
    material.name = query.value(2).toString();
    material.specification = query.value(3).toString();
    material.unit = query.value(4).toString();
    material.category = query.value(5).toString();
    material.currentUnitPriceCents = query.value(6).toLongLong();
    material.isEnabled = query.value(7).toBool();
    material.revision = query.value(8).toUInt();
    material.createdAt = query.value(9).toDateTime();
    material.updatedAt = query.value(10).toDateTime();
    return material;
}

Customer readCustomer(const QSqlQuery& query) {
    Customer customer;
    customer.id = query.value(0).toLongLong();
    customer.name = query.value(1).toString();
    customer.contactName = query.value(2).toString();
    customer.phone = query.value(3).toString();
    customer.address = query.value(4).toString();
    customer.notes = query.value(5).toString();
    customer.revision = query.value(6).toUInt();
    customer.createdAt = query.value(7).toDateTime();
    customer.updatedAt = query.value(8).toDateTime();
    return customer;
}

RepositoryErrorCode sqlErrorCode(const QSqlError& error) {
    return error.nativeErrorCode() == QStringLiteral("1062")
               ? RepositoryErrorCode::Duplicate
               : RepositoryErrorCode::Database;
}

QString sqlString(const QString& value) {
    return value.isNull() ? QStringLiteral("") : value;
}

void bindMaterialDraft(QSqlQuery* query, const MaterialDraft& draft) {
    query->bindValue(QStringLiteral(":code"), sqlString(draft.code));
    query->bindValue(QStringLiteral(":name"), sqlString(draft.name));
    query->bindValue(
        QStringLiteral(":specification"),
        sqlString(draft.specification)
    );
    query->bindValue(QStringLiteral(":unit"), sqlString(draft.unit));
    query->bindValue(QStringLiteral(":category"), sqlString(draft.category));
    query->bindValue(
        QStringLiteral(":currentUnitPriceCents"),
        QVariant::fromValue<qlonglong>(draft.currentUnitPriceCents)
    );
    query->bindValue(QStringLiteral(":isEnabled"), draft.isEnabled);
}

void bindCustomerDraft(QSqlQuery* query, const CustomerDraft& draft) {
    query->bindValue(QStringLiteral(":name"), sqlString(draft.name));
    query->bindValue(
        QStringLiteral(":contactName"),
        sqlString(draft.contactName)
    );
    query->bindValue(QStringLiteral(":phone"), sqlString(draft.phone));
    query->bindValue(QStringLiteral(":address"), sqlString(draft.address));
    query->bindValue(QStringLiteral(":notes"), sqlString(draft.notes));
}

} // namespace

MySqlCatalogRepository::MySqlCatalogRepository(QSqlDatabase database)
    : database_(std::move(database)) {}

void MySqlCatalogRepository::clearError(RepositoryError* error) {
    if (error != nullptr) {
        *error = {};
    }
}

void MySqlCatalogRepository::setError(
    RepositoryError* error,
    RepositoryErrorCode code,
    const QString& message
) {
    if (error != nullptr) {
        error->code = code;
        error->message = message;
    }
}

bool MySqlCatalogRepository::recordExists(
    const QString& table,
    std::int64_t id,
    bool* exists,
    RepositoryError* error
) {
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("SELECT 1 FROM %1 WHERE id = :id").arg(table));
    query.bindValue(QStringLiteral(":id"), QVariant::fromValue<qlonglong>(id));
    if (!query.exec()) {
        setError(error, RepositoryErrorCode::Database, query.lastError().text());
        return false;
    }
    *exists = query.next();
    return true;
}

bool MySqlCatalogRepository::listMaterials(
    const PageQuery& request,
    Page<Material>* page,
    RepositoryError* error
) {
    clearError(error);
    QStringList conditions;
    if (!request.search.isEmpty()) {
        conditions.append(QStringLiteral(
            "(LOCATE(:searchCode, code) > 0 OR LOCATE(:searchName, name) > 0 "
            "OR LOCATE(:searchCategory, category) > 0)"
        ));
    }
    if (request.enabled.has_value()) {
        conditions.append(QStringLiteral("is_enabled = :enabled"));
    }
    const auto where = conditions.isEmpty()
                           ? QString()
                           : QStringLiteral(" WHERE %1").arg(
                                 conditions.join(QStringLiteral(" AND "))
                             );

    const auto bindFilters = [&](QSqlQuery* query) {
        if (!request.search.isEmpty()) {
            query->bindValue(QStringLiteral(":searchCode"), request.search);
            query->bindValue(QStringLiteral(":searchName"), request.search);
            query->bindValue(QStringLiteral(":searchCategory"), request.search);
        }
        if (request.enabled.has_value()) {
            query->bindValue(QStringLiteral(":enabled"), *request.enabled);
        }
    };

    QSqlQuery countQuery(database_);
    countQuery.prepare(QStringLiteral("SELECT COUNT(*) FROM materials%1").arg(where));
    bindFilters(&countQuery);
    if (!countQuery.exec() || !countQuery.next()) {
        setError(error, RepositoryErrorCode::Database, countQuery.lastError().text());
        return false;
    }

    page->items.clear();
    page->total = countQuery.value(0).toLongLong();
    page->page = request.page;
    page->pageSize = request.pageSize;

    const auto offset =
        (static_cast<qint64>(request.page) - 1) * request.pageSize;
    QSqlQuery itemsQuery(database_);
    itemsQuery.prepare(
        QStringLiteral("SELECT %1 FROM materials%2 ORDER BY id DESC LIMIT %3 OFFSET %4")
            .arg(QString::fromLatin1(kMaterialColumns), where)
            .arg(request.pageSize)
            .arg(offset)
    );
    bindFilters(&itemsQuery);
    if (!itemsQuery.exec()) {
        setError(error, RepositoryErrorCode::Database, itemsQuery.lastError().text());
        return false;
    }
    while (itemsQuery.next()) {
        page->items.push_back(readMaterial(itemsQuery));
    }
    return true;
}

bool MySqlCatalogRepository::findMaterial(
    std::int64_t id,
    Material* material,
    RepositoryError* error
) {
    clearError(error);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("SELECT %1 FROM materials WHERE id = :id")
                      .arg(QString::fromLatin1(kMaterialColumns)));
    query.bindValue(QStringLiteral(":id"), QVariant::fromValue<qlonglong>(id));
    if (!query.exec()) {
        setError(error, RepositoryErrorCode::Database, query.lastError().text());
        return false;
    }
    if (!query.next()) {
        setError(error, RepositoryErrorCode::NotFound, QStringLiteral("material not found"));
        return false;
    }
    *material = readMaterial(query);
    return true;
}

bool MySqlCatalogRepository::createMaterial(
    const MaterialDraft& draft,
    Material* material,
    RepositoryError* error
) {
    clearError(error);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO materials "
        "(code, name, specification, unit, category, current_unit_price_cents, is_enabled) "
        "VALUES (:code, :name, :specification, :unit, :category, "
        ":currentUnitPriceCents, :isEnabled)"
    ));
    bindMaterialDraft(&query, draft);
    if (!query.exec()) {
        setError(error, sqlErrorCode(query.lastError()), query.lastError().text());
        return false;
    }
    return findMaterial(query.lastInsertId().toLongLong(), material, error);
}

bool MySqlCatalogRepository::updateMaterial(
    std::int64_t id,
    std::uint32_t expectedRevision,
    const MaterialDraft& draft,
    Material* material,
    RepositoryError* error
) {
    clearError(error);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE materials SET code = :code, name = :name, "
        "specification = :specification, unit = :unit, category = :category, "
        "current_unit_price_cents = :currentUnitPriceCents, "
        "is_enabled = :isEnabled, revision = revision + 1 "
        "WHERE id = :id AND revision = :revision"
    ));
    bindMaterialDraft(&query, draft);
    query.bindValue(QStringLiteral(":id"), QVariant::fromValue<qlonglong>(id));
    query.bindValue(QStringLiteral(":revision"), expectedRevision);
    if (!query.exec()) {
        setError(error, sqlErrorCode(query.lastError()), query.lastError().text());
        return false;
    }
    if (query.numRowsAffected() == 0) {
        bool exists = false;
        if (!recordExists(QStringLiteral("materials"), id, &exists, error)) {
            return false;
        }
        setError(
            error,
            exists ? RepositoryErrorCode::RevisionConflict
                   : RepositoryErrorCode::NotFound,
            exists ? QStringLiteral("material revision conflict")
                   : QStringLiteral("material not found")
        );
        return false;
    }
    return findMaterial(id, material, error);
}

bool MySqlCatalogRepository::setMaterialEnabled(
    std::int64_t id,
    std::uint32_t expectedRevision,
    bool enabled,
    Material* material,
    RepositoryError* error
) {
    clearError(error);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE materials SET is_enabled = :enabled, revision = revision + 1 "
        "WHERE id = :id AND revision = :revision"
    ));
    query.bindValue(QStringLiteral(":enabled"), enabled);
    query.bindValue(QStringLiteral(":id"), QVariant::fromValue<qlonglong>(id));
    query.bindValue(QStringLiteral(":revision"), expectedRevision);
    if (!query.exec()) {
        setError(error, RepositoryErrorCode::Database, query.lastError().text());
        return false;
    }
    if (query.numRowsAffected() == 0) {
        bool exists = false;
        if (!recordExists(QStringLiteral("materials"), id, &exists, error)) {
            return false;
        }
        setError(
            error,
            exists ? RepositoryErrorCode::RevisionConflict
                   : RepositoryErrorCode::NotFound,
            exists ? QStringLiteral("material revision conflict")
                   : QStringLiteral("material not found")
        );
        return false;
    }
    return findMaterial(id, material, error);
}

bool MySqlCatalogRepository::listCustomers(
    const PageQuery& request,
    Page<Customer>* page,
    RepositoryError* error
) {
    clearError(error);
    const auto where = request.search.isEmpty()
                           ? QString()
                           : QStringLiteral(
                                 " WHERE LOCATE(:searchName, name) > 0 "
                                 "OR LOCATE(:searchContact, contact_name) > 0 "
                                 "OR LOCATE(:searchPhone, phone) > 0"
                             );
    const auto bindSearch = [&](QSqlQuery* query) {
        if (!request.search.isEmpty()) {
            query->bindValue(QStringLiteral(":searchName"), request.search);
            query->bindValue(QStringLiteral(":searchContact"), request.search);
            query->bindValue(QStringLiteral(":searchPhone"), request.search);
        }
    };

    QSqlQuery countQuery(database_);
    countQuery.prepare(QStringLiteral("SELECT COUNT(*) FROM customers%1").arg(where));
    bindSearch(&countQuery);
    if (!countQuery.exec() || !countQuery.next()) {
        setError(error, RepositoryErrorCode::Database, countQuery.lastError().text());
        return false;
    }

    page->items.clear();
    page->total = countQuery.value(0).toLongLong();
    page->page = request.page;
    page->pageSize = request.pageSize;
    const auto offset =
        (static_cast<qint64>(request.page) - 1) * request.pageSize;

    QSqlQuery itemsQuery(database_);
    itemsQuery.prepare(
        QStringLiteral("SELECT %1 FROM customers%2 ORDER BY id DESC LIMIT %3 OFFSET %4")
            .arg(QString::fromLatin1(kCustomerColumns), where)
            .arg(request.pageSize)
            .arg(offset)
    );
    bindSearch(&itemsQuery);
    if (!itemsQuery.exec()) {
        setError(error, RepositoryErrorCode::Database, itemsQuery.lastError().text());
        return false;
    }
    while (itemsQuery.next()) {
        page->items.push_back(readCustomer(itemsQuery));
    }
    return true;
}

bool MySqlCatalogRepository::findCustomer(
    std::int64_t id,
    Customer* customer,
    RepositoryError* error
) {
    clearError(error);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("SELECT %1 FROM customers WHERE id = :id")
                      .arg(QString::fromLatin1(kCustomerColumns)));
    query.bindValue(QStringLiteral(":id"), QVariant::fromValue<qlonglong>(id));
    if (!query.exec()) {
        setError(error, RepositoryErrorCode::Database, query.lastError().text());
        return false;
    }
    if (!query.next()) {
        setError(error, RepositoryErrorCode::NotFound, QStringLiteral("customer not found"));
        return false;
    }
    *customer = readCustomer(query);
    return true;
}

bool MySqlCatalogRepository::createCustomer(
    const CustomerDraft& draft,
    Customer* customer,
    RepositoryError* error
) {
    clearError(error);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO customers (name, contact_name, phone, address, notes) "
        "VALUES (:name, :contactName, :phone, :address, :notes)"
    ));
    bindCustomerDraft(&query, draft);
    if (!query.exec()) {
        setError(error, RepositoryErrorCode::Database, query.lastError().text());
        return false;
    }
    return findCustomer(query.lastInsertId().toLongLong(), customer, error);
}

bool MySqlCatalogRepository::updateCustomer(
    std::int64_t id,
    std::uint32_t expectedRevision,
    const CustomerDraft& draft,
    Customer* customer,
    RepositoryError* error
) {
    clearError(error);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE customers SET name = :name, contact_name = :contactName, "
        "phone = :phone, address = :address, notes = :notes, "
        "revision = revision + 1 WHERE id = :id AND revision = :revision"
    ));
    bindCustomerDraft(&query, draft);
    query.bindValue(QStringLiteral(":id"), QVariant::fromValue<qlonglong>(id));
    query.bindValue(QStringLiteral(":revision"), expectedRevision);
    if (!query.exec()) {
        setError(error, RepositoryErrorCode::Database, query.lastError().text());
        return false;
    }
    if (query.numRowsAffected() == 0) {
        bool exists = false;
        if (!recordExists(QStringLiteral("customers"), id, &exists, error)) {
            return false;
        }
        setError(
            error,
            exists ? RepositoryErrorCode::RevisionConflict
                   : RepositoryErrorCode::NotFound,
            exists ? QStringLiteral("customer revision conflict")
                   : QStringLiteral("customer not found")
        );
        return false;
    }
    return findCustomer(id, customer, error);
}

} // namespace manage::data
