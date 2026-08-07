#include "manage/data/catalog_repository.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <utility>

namespace manage::data {
namespace {

constexpr auto kMaterialColumns =
    "id, code, name, specification, unit, category, is_copper_based, "
    "current_unit_price_cents, is_enabled, revision, created_at, updated_at";
constexpr auto kCustomerColumns =
    "id, name, contact_name, phone, address, notes, revision, created_at, updated_at";
constexpr auto kMaterialSupplierColumns =
    "id, material_id, supplier_name, contact_name, phone, is_default, "
    "is_enabled, lead_days, revision, created_at, updated_at";
constexpr auto kMaterialPriceColumns =
    "id, material_supplier_id, copper_price_cents, unit_price_cents, is_default, "
    "is_enabled, revision, created_at, updated_at";

Material readMaterial(const QSqlQuery& query) {
    Material material;
    material.id = query.value(0).toLongLong();
    material.code = query.value(1).toString();
    material.name = query.value(2).toString();
    material.specification = query.value(3).toString();
    material.unit = query.value(4).toString();
    material.category = query.value(5).toString();
    material.isCopperBased = query.value(6).toBool();
    material.currentUnitPriceCents = query.value(7).toLongLong();
    material.isEnabled = query.value(8).toBool();
    material.revision = query.value(9).toUInt();
    material.createdAt = query.value(10).toDateTime();
    material.updatedAt = query.value(11).toDateTime();
    return material;
}

MaterialSupplier readMaterialSupplier(const QSqlQuery& query) {
    MaterialSupplier supplier;
    supplier.id = query.value(0).toLongLong();
    supplier.materialId = query.value(1).toLongLong();
    supplier.supplierName = query.value(2).toString();
    supplier.contactName = query.value(3).toString();
    supplier.phone = query.value(4).toString();
    supplier.isDefault = query.value(5).toBool();
    supplier.isEnabled = query.value(6).toBool();
    supplier.leadDays = query.value(7).toInt();
    supplier.revision = query.value(8).toUInt();
    supplier.createdAt = query.value(9).toDateTime();
    supplier.updatedAt = query.value(10).toDateTime();
    return supplier;
}

MaterialPrice readMaterialPrice(const QSqlQuery& query) {
    MaterialPrice price;
    price.id = query.value(0).toLongLong();
    price.supplierId = query.value(1).toLongLong();
    if (!query.value(2).isNull()) {
        price.copperPriceCents = query.value(2).toLongLong();
    }
    price.unitPriceCents = query.value(3).toLongLong();
    price.isDefault = query.value(4).toBool();
    price.isEnabled = query.value(5).toBool();
    price.revision = query.value(6).toUInt();
    price.createdAt = query.value(7).toDateTime();
    price.updatedAt = query.value(8).toDateTime();
    return price;
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
    query->bindValue(QStringLiteral(":isCopperBased"), draft.isCopperBased);
    query->bindValue(
        QStringLiteral(":currentUnitPriceCents"),
        QVariant::fromValue<qlonglong>(draft.currentUnitPriceCents)
    );
    query->bindValue(QStringLiteral(":isEnabled"), draft.isEnabled);
}

void bindMaterialSupplierDraft(QSqlQuery* query, const MaterialSupplierDraft& draft) {
    query->bindValue(QStringLiteral(":supplierName"), sqlString(draft.supplierName));
    query->bindValue(QStringLiteral(":contactName"), sqlString(draft.contactName));
    query->bindValue(QStringLiteral(":phone"), sqlString(draft.phone));
    query->bindValue(QStringLiteral(":isDefault"), draft.isDefault);
    query->bindValue(QStringLiteral(":isEnabled"), draft.isEnabled);
    query->bindValue(QStringLiteral(":leadDays"), draft.leadDays);
}

void bindMaterialPriceDraft(QSqlQuery* query, const MaterialPriceDraft& draft) {
    if (draft.copperPriceCents.has_value()) {
        query->bindValue(
            QStringLiteral(":copperPriceCents"),
            QVariant::fromValue<qlonglong>(*draft.copperPriceCents)
        );
    } else {
        query->bindValue(QStringLiteral(":copperPriceCents"), QVariant(QVariant::LongLong));
    }
    query->bindValue(
        QStringLiteral(":unitPriceCents"),
        QVariant::fromValue<qlonglong>(draft.unitPriceCents)
    );
    query->bindValue(QStringLiteral(":isDefault"), draft.isDefault);
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
        "(code, name, specification, unit, category, is_copper_based, "
        "current_unit_price_cents, is_enabled) "
        "VALUES (:code, :name, :specification, :unit, :category, :isCopperBased, "
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
        "is_copper_based = :isCopperBased, "
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

bool MySqlCatalogRepository::listMaterialSuppliers(
    std::int64_t materialId,
    const PageQuery& request,
    Page<MaterialSupplier>* page,
    RepositoryError* error
) {
    clearError(error);
    const auto where = request.search.isEmpty()
                           ? QStringLiteral(" WHERE material_id = :materialId")
                           : QStringLiteral(
                                 " WHERE material_id = :materialId AND "
                                 "(LOCATE(:searchName, supplier_name) > 0 OR "
                                 "LOCATE(:searchContact, contact_name) > 0)"
                             );
    const auto bindSearch = [&](QSqlQuery* query) {
        query->bindValue(
            QStringLiteral(":materialId"),
            QVariant::fromValue<qlonglong>(materialId)
        );
        if (!request.search.isEmpty()) {
            query->bindValue(QStringLiteral(":searchName"), request.search);
            query->bindValue(QStringLiteral(":searchContact"), request.search);
        }
    };

    QSqlQuery countQuery(database_);
    countQuery.prepare(
        QStringLiteral("SELECT COUNT(*) FROM material_suppliers%1").arg(where)
    );
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
        QStringLiteral(
            "SELECT %1 FROM material_suppliers%2 "
            "ORDER BY is_default DESC, id ASC LIMIT %3 OFFSET %4"
        )
            .arg(QString::fromLatin1(kMaterialSupplierColumns), where)
            .arg(request.pageSize)
            .arg(offset)
    );
    bindSearch(&itemsQuery);
    if (!itemsQuery.exec()) {
        setError(error, RepositoryErrorCode::Database, itemsQuery.lastError().text());
        return false;
    }
    while (itemsQuery.next()) {
        page->items.push_back(readMaterialSupplier(itemsQuery));
    }
    return true;
}

bool MySqlCatalogRepository::findMaterialSupplier(
    std::int64_t id,
    MaterialSupplier* supplier,
    RepositoryError* error
) {
    clearError(error);
    QSqlQuery query(database_);
    query.prepare(
        QStringLiteral("SELECT %1 FROM material_suppliers WHERE id = :id")
            .arg(QString::fromLatin1(kMaterialSupplierColumns))
    );
    query.bindValue(QStringLiteral(":id"), QVariant::fromValue<qlonglong>(id));
    if (!query.exec()) {
        setError(error, RepositoryErrorCode::Database, query.lastError().text());
        return false;
    }
    if (!query.next()) {
        setError(error, RepositoryErrorCode::NotFound, QStringLiteral("material supplier not found"));
        return false;
    }
    *supplier = readMaterialSupplier(query);
    return true;
}

bool MySqlCatalogRepository::createMaterialSupplier(
    std::int64_t materialId,
    const MaterialSupplierDraft& draft,
    MaterialSupplier* supplier,
    RepositoryError* error
) {
    clearError(error);
    bool materialExists = false;
    if (!recordExists(QStringLiteral("materials"), materialId, &materialExists, error)) {
        return false;
    }
    if (!materialExists) {
        setError(
            error,
            RepositoryErrorCode::NotFound,
            QStringLiteral("material not found")
        );
        return false;
    }

    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO material_suppliers "
        "(material_id, supplier_name, contact_name, phone, is_default, is_enabled, lead_days) "
        "VALUES (:materialId, :supplierName, :contactName, :phone, :isDefault, :isEnabled, :leadDays)"
    ));
    query.bindValue(
        QStringLiteral(":materialId"),
        QVariant::fromValue<qlonglong>(materialId)
    );
    bindMaterialSupplierDraft(&query, draft);
    if (!query.exec()) {
        setError(error, sqlErrorCode(query.lastError()), query.lastError().text());
        return false;
    }
    return findMaterialSupplier(query.lastInsertId().toLongLong(), supplier, error);
}

bool MySqlCatalogRepository::updateMaterialSupplier(
    std::int64_t id,
    std::uint32_t expectedRevision,
    const MaterialSupplierDraft& draft,
    MaterialSupplier* supplier,
    RepositoryError* error
) {
    clearError(error);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE material_suppliers SET supplier_name = :supplierName, "
        "contact_name = :contactName, phone = :phone, is_default = :isDefault, "
        "is_enabled = :isEnabled, lead_days = :leadDays, revision = revision + 1 "
        "WHERE id = :id AND revision = :revision"
    ));
    bindMaterialSupplierDraft(&query, draft);
    query.bindValue(QStringLiteral(":id"), QVariant::fromValue<qlonglong>(id));
    query.bindValue(QStringLiteral(":revision"), expectedRevision);
    if (!query.exec()) {
        setError(error, sqlErrorCode(query.lastError()), query.lastError().text());
        return false;
    }
    if (query.numRowsAffected() == 0) {
        bool exists = false;
        if (!recordExists(QStringLiteral("material_suppliers"), id, &exists, error)) {
            return false;
        }
        setError(
            error,
            exists ? RepositoryErrorCode::RevisionConflict
                   : RepositoryErrorCode::NotFound,
            exists ? QStringLiteral("material supplier revision conflict")
                   : QStringLiteral("material supplier not found")
        );
        return false;
    }
    return findMaterialSupplier(id, supplier, error);
}

bool MySqlCatalogRepository::setMaterialSupplierEnabled(
    std::int64_t id,
    std::uint32_t expectedRevision,
    bool enabled,
    MaterialSupplier* supplier,
    RepositoryError* error
) {
    clearError(error);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE material_suppliers SET is_enabled = :enabled, "
        "revision = revision + 1 WHERE id = :id AND revision = :revision"
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
        if (!recordExists(QStringLiteral("material_suppliers"), id, &exists, error)) {
            return false;
        }
        setError(
            error,
            exists ? RepositoryErrorCode::RevisionConflict
                   : RepositoryErrorCode::NotFound,
            exists ? QStringLiteral("material supplier revision conflict")
                   : QStringLiteral("material supplier not found")
        );
        return false;
    }
    return findMaterialSupplier(id, supplier, error);
}

bool MySqlCatalogRepository::listMaterialPrices(
    std::int64_t supplierId,
    const PageQuery& request,
    Page<MaterialPrice>* page,
    RepositoryError* error
) {
    clearError(error);
    const auto where = QStringLiteral(" WHERE material_supplier_id = :supplierId");
    const auto bindSearch = [&](QSqlQuery* query) {
        query->bindValue(
            QStringLiteral(":supplierId"),
            QVariant::fromValue<qlonglong>(supplierId)
        );
    };

    QSqlQuery countQuery(database_);
    countQuery.prepare(
        QStringLiteral("SELECT COUNT(*) FROM material_supplier_prices%1").arg(where)
    );
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
        QStringLiteral(
            "SELECT %1 FROM material_supplier_prices%2 "
            "ORDER BY copper_price_cents IS NOT NULL DESC, "
            "copper_price_cents ASC, is_default DESC, id ASC LIMIT %3 OFFSET %4"
        )
            .arg(QString::fromLatin1(kMaterialPriceColumns), where)
            .arg(request.pageSize)
            .arg(offset)
    );
    bindSearch(&itemsQuery);
    if (!itemsQuery.exec()) {
        setError(error, RepositoryErrorCode::Database, itemsQuery.lastError().text());
        return false;
    }
    while (itemsQuery.next()) {
        page->items.push_back(readMaterialPrice(itemsQuery));
    }
    return true;
}

bool MySqlCatalogRepository::findMaterialPrice(
    std::int64_t id,
    MaterialPrice* price,
    RepositoryError* error
) {
    clearError(error);
    QSqlQuery query(database_);
    query.prepare(
        QStringLiteral("SELECT %1 FROM material_supplier_prices WHERE id = :id")
            .arg(QString::fromLatin1(kMaterialPriceColumns))
    );
    query.bindValue(QStringLiteral(":id"), QVariant::fromValue<qlonglong>(id));
    if (!query.exec()) {
        setError(error, RepositoryErrorCode::Database, query.lastError().text());
        return false;
    }
    if (!query.next()) {
        setError(error, RepositoryErrorCode::NotFound, QStringLiteral("material price not found"));
        return false;
    }
    *price = readMaterialPrice(query);
    return true;
}

bool MySqlCatalogRepository::createMaterialPrice(
    std::int64_t supplierId,
    const MaterialPriceDraft& draft,
    MaterialPrice* price,
    RepositoryError* error
) {
    clearError(error);
    bool supplierExists = false;
    if (!recordExists(
            QStringLiteral("material_suppliers"),
            supplierId,
            &supplierExists,
            error
        )) {
        return false;
    }
    if (!supplierExists) {
        setError(
            error,
            RepositoryErrorCode::NotFound,
            QStringLiteral("material supplier not found")
        );
        return false;
    }

    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO material_supplier_prices "
        "(material_supplier_id, copper_price_cents, unit_price_cents, "
        "is_default, is_enabled) "
        "VALUES (:supplierId, :copperPriceCents, :unitPriceCents, "
        ":isDefault, :isEnabled)"
    ));
    query.bindValue(
        QStringLiteral(":supplierId"),
        QVariant::fromValue<qlonglong>(supplierId)
    );
    bindMaterialPriceDraft(&query, draft);
    if (!query.exec()) {
        setError(error, sqlErrorCode(query.lastError()), query.lastError().text());
        return false;
    }
    return findMaterialPrice(query.lastInsertId().toLongLong(), price, error);
}

bool MySqlCatalogRepository::updateMaterialPrice(
    std::int64_t id,
    std::uint32_t expectedRevision,
    const MaterialPriceDraft& draft,
    MaterialPrice* price,
    RepositoryError* error
) {
    clearError(error);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE material_supplier_prices SET "
        "copper_price_cents = :copperPriceCents, "
        "unit_price_cents = :unitPriceCents, is_default = :isDefault, "
        "is_enabled = :isEnabled, revision = revision + 1 "
        "WHERE id = :id AND revision = :revision"
    ));
    bindMaterialPriceDraft(&query, draft);
    query.bindValue(QStringLiteral(":id"), QVariant::fromValue<qlonglong>(id));
    query.bindValue(QStringLiteral(":revision"), expectedRevision);
    if (!query.exec()) {
        setError(error, sqlErrorCode(query.lastError()), query.lastError().text());
        return false;
    }
    if (query.numRowsAffected() == 0) {
        bool exists = false;
        if (!recordExists(QStringLiteral("material_supplier_prices"), id, &exists, error)) {
            return false;
        }
        setError(
            error,
            exists ? RepositoryErrorCode::RevisionConflict
                   : RepositoryErrorCode::NotFound,
            exists ? QStringLiteral("material price revision conflict")
                   : QStringLiteral("material price not found")
        );
        return false;
    }
    return findMaterialPrice(id, price, error);
}

bool MySqlCatalogRepository::setMaterialPriceEnabled(
    std::int64_t id,
    std::uint32_t expectedRevision,
    bool enabled,
    MaterialPrice* price,
    RepositoryError* error
) {
    clearError(error);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE material_supplier_prices SET is_enabled = :enabled, "
        "revision = revision + 1 WHERE id = :id AND revision = :revision"
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
        if (!recordExists(QStringLiteral("material_supplier_prices"), id, &exists, error)) {
            return false;
        }
        setError(
            error,
            exists ? RepositoryErrorCode::RevisionConflict
                   : RepositoryErrorCode::NotFound,
            exists ? QStringLiteral("material price revision conflict")
                   : QStringLiteral("material price not found")
        );
        return false;
    }
    return findMaterialPrice(id, price, error);
}

bool MySqlCatalogRepository::createMaterialBundle(
    const MaterialBundleDraft& bundle,
    MaterialBundleResult* result,
    RepositoryError* error
) {
    clearError(error);
    if (result == nullptr) {
        setError(
            error,
            RepositoryErrorCode::Database,
            QStringLiteral("result buffer is required")
        );
        return false;
    }
    if (!database_.transaction()) {
        setError(
            error,
            RepositoryErrorCode::Database,
            QStringLiteral("unable to begin transaction: %1")
                .arg(database_.lastError().text())
        );
        return false;
    }

    const auto rollback = [this, error]() {
        database_.rollback();
        if (error->code == RepositoryErrorCode::None) {
            setError(
                error,
                RepositoryErrorCode::Database,
                QStringLiteral("transaction rolled back")
            );
        }
    };

    // 1. 物料
    {
        QSqlQuery query(database_);
        query.prepare(QStringLiteral(
            "INSERT INTO materials "
            "(code, name, specification, unit, category, is_copper_based, "
            "current_unit_price_cents, is_enabled) "
            "VALUES (:code, :name, :specification, :unit, :category, :isCopperBased, "
            ":currentUnitPriceCents, :isEnabled)"
        ));
        bindMaterialDraft(&query, bundle.material);
        if (!query.exec()) {
            setError(error, sqlErrorCode(query.lastError()), query.lastError().text());
            rollback();
            return false;
        }
        const auto materialId = query.lastInsertId().toLongLong();
        if (!findMaterial(materialId, &result->material, error)) {
            rollback();
            return false;
        }
    }

    // 2. 供应商及其价格（同一供应商可对应多条铜价价格）
    result->suppliers.reserve(bundle.suppliers.size());
    for (const auto& entry : bundle.suppliers) {
        QSqlQuery supplierQuery(database_);
        supplierQuery.prepare(QStringLiteral(
            "INSERT INTO material_suppliers "
            "(material_id, supplier_name, contact_name, phone, is_default, is_enabled, lead_days) "
            "VALUES (:materialId, :supplierName, :contactName, :phone, :isDefault, :isEnabled, :leadDays)"
        ));
        supplierQuery.bindValue(
            QStringLiteral(":materialId"),
            QVariant::fromValue<qlonglong>(result->material.id)
        );
        bindMaterialSupplierDraft(&supplierQuery, entry.supplier);
        if (!supplierQuery.exec()) {
            setError(
                error,
                sqlErrorCode(supplierQuery.lastError()),
                supplierQuery.lastError().text()
            );
            rollback();
            return false;
        }
        const auto supplierId = supplierQuery.lastInsertId().toLongLong();
        MaterialSupplier supplier;
        if (!findMaterialSupplier(supplierId, &supplier, error)) {
            rollback();
            return false;
        }
        result->suppliers.push_back(supplier);

        for (const auto& priceDraft : entry.prices) {
            QSqlQuery priceQuery(database_);
            priceQuery.prepare(QStringLiteral(
                "INSERT INTO material_supplier_prices "
                "(material_supplier_id, copper_price_cents, unit_price_cents, "
                "is_default, is_enabled) "
                "VALUES (:supplierId, :copperPriceCents, :unitPriceCents, "
                ":isDefault, :isEnabled)"
            ));
            priceQuery.bindValue(
                QStringLiteral(":supplierId"),
                QVariant::fromValue<qlonglong>(supplierId)
            );
            bindMaterialPriceDraft(&priceQuery, priceDraft);
            if (!priceQuery.exec()) {
                setError(
                    error,
                    sqlErrorCode(priceQuery.lastError()),
                    priceQuery.lastError().text()
                );
                rollback();
                return false;
            }
            MaterialPrice price;
            if (!findMaterialPrice(priceQuery.lastInsertId().toLongLong(), &price, error)) {
                rollback();
                return false;
            }
            result->prices.push_back(price);
        }
    }

    if (!database_.commit()) {
        setError(
            error,
            RepositoryErrorCode::Database,
            QStringLiteral("unable to commit bundle: %1")
                .arg(database_.lastError().text())
        );
        database_.rollback();
        return false;
    }
    return true;
}

// 整包替换（编辑模式）：乐观锁更新物料，删除其下全部供应商与价格后重新写入。
// 迁移 004 已通过 ON DELETE CASCADE 让 material_supplier_prices 跟随 suppliers 级联删除。
bool MySqlCatalogRepository::replaceMaterialBundle(
    std::int64_t materialId,
    std::uint32_t expectedRevision,
    const MaterialBundleDraft& bundle,
    MaterialBundleResult* result,
    RepositoryError* error
) {
    clearError(error);
    if (result == nullptr) {
        setError(
            error,
            RepositoryErrorCode::Database,
            QStringLiteral("result buffer is required")
        );
        return false;
    }
    if (!database_.transaction()) {
        setError(
            error,
            RepositoryErrorCode::Database,
            QStringLiteral("unable to begin transaction: %1")
                .arg(database_.lastError().text())
        );
        return false;
    }

    const auto rollback = [this, error]() {
        database_.rollback();
        if (error->code == RepositoryErrorCode::None) {
            setError(
                error,
                RepositoryErrorCode::Database,
                QStringLiteral("transaction rolled back")
            );
        }
    };

    // 1. 乐观锁更新物料
    {
        QSqlQuery query(database_);
        query.prepare(QStringLiteral(
            "UPDATE materials SET code = :code, name = :name, "
            "specification = :specification, unit = :unit, category = :category, "
            "is_copper_based = :isCopperBased, "
            "current_unit_price_cents = :currentUnitPriceCents, "
            "is_enabled = :isEnabled, revision = revision + 1 "
            "WHERE id = :id AND revision = :revision"
        ));
        bindMaterialDraft(&query, bundle.material);
        query.bindValue(QStringLiteral(":id"), QVariant::fromValue<qlonglong>(materialId));
        query.bindValue(QStringLiteral(":revision"), expectedRevision);
        if (!query.exec()) {
            setError(error, sqlErrorCode(query.lastError()), query.lastError().text());
            rollback();
            return false;
        }
        if (query.numRowsAffected() == 0) {
            bool exists = false;
            if (!recordExists(QStringLiteral("materials"), materialId, &exists, error)) {
                rollback();
                return false;
            }
            setError(
                error,
                exists ? RepositoryErrorCode::RevisionConflict
                       : RepositoryErrorCode::NotFound,
                exists ? QStringLiteral("material revision conflict")
                       : QStringLiteral("material not found")
            );
            rollback();
            return false;
        }
        if (!findMaterial(materialId, &result->material, error)) {
            rollback();
            return false;
        }
    }

    // 2. 删除该物料下全部供应商：material_supplier_prices 跟随级联删除。
    {
        QSqlQuery query(database_);
        query.prepare(QStringLiteral(
            "DELETE FROM material_suppliers WHERE material_id = :materialId"
        ));
        query.bindValue(
            QStringLiteral(":materialId"),
            QVariant::fromValue<qlonglong>(materialId)
        );
        if (!query.exec()) {
            setError(
                error,
                sqlErrorCode(query.lastError()),
                query.lastError().text()
            );
            rollback();
            return false;
        }
    }

    // 3. 写入新供应商和价格（与 createMaterialBundle 逻辑一致）。
    result->suppliers.reserve(bundle.suppliers.size());
    for (const auto& entry : bundle.suppliers) {
        QSqlQuery supplierQuery(database_);
        supplierQuery.prepare(QStringLiteral(
            "INSERT INTO material_suppliers "
            "(material_id, supplier_name, contact_name, phone, is_default, is_enabled, lead_days) "
            "VALUES (:materialId, :supplierName, :contactName, :phone, :isDefault, :isEnabled, :leadDays)"
        ));
        supplierQuery.bindValue(
            QStringLiteral(":materialId"),
            QVariant::fromValue<qlonglong>(materialId)
        );
        bindMaterialSupplierDraft(&supplierQuery, entry.supplier);
        if (!supplierQuery.exec()) {
            setError(
                error,
                sqlErrorCode(supplierQuery.lastError()),
                supplierQuery.lastError().text()
            );
            rollback();
            return false;
        }
        const auto supplierId = supplierQuery.lastInsertId().toLongLong();
        MaterialSupplier supplier;
        if (!findMaterialSupplier(supplierId, &supplier, error)) {
            rollback();
            return false;
        }
        result->suppliers.push_back(supplier);

        for (const auto& priceDraft : entry.prices) {
            QSqlQuery priceQuery(database_);
            priceQuery.prepare(QStringLiteral(
                "INSERT INTO material_supplier_prices "
                "(material_supplier_id, copper_price_cents, unit_price_cents, "
                "is_default, is_enabled) "
                "VALUES (:supplierId, :copperPriceCents, :unitPriceCents, "
                ":isDefault, :isEnabled)"
            ));
            priceQuery.bindValue(
                QStringLiteral(":supplierId"),
                QVariant::fromValue<qlonglong>(supplierId)
            );
            bindMaterialPriceDraft(&priceQuery, priceDraft);
            if (!priceQuery.exec()) {
                setError(
                    error,
                    sqlErrorCode(priceQuery.lastError()),
                    priceQuery.lastError().text()
                );
                rollback();
                return false;
            }
            MaterialPrice price;
            if (!findMaterialPrice(priceQuery.lastInsertId().toLongLong(), &price, error)) {
                rollback();
                return false;
            }
            result->prices.push_back(price);
        }
    }

    if (!database_.commit()) {
        setError(
            error,
            RepositoryErrorCode::Database,
            QStringLiteral("unable to commit bundle: %1")
                .arg(database_.lastError().text())
        );
        database_.rollback();
        return false;
    }
    return true;
}

} // namespace manage::data
