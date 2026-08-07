#include "manage/server/catalog_routes.h"

#include "manage/server/http_authorization.h"

#include "manage/data/catalog_service.h"

#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponder>
#include <QHttpServerResponse>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QUrlQuery>

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

namespace manage::server {
namespace {

using StatusCode = QHttpServerResponder::StatusCode;

constexpr qint64 kMaxSafeJsonInteger = 9'007'199'254'740'991;

QString errorCode(manage::data::CatalogErrorCode code) {
    using Code = manage::data::CatalogErrorCode;
    switch (code) {
    case Code::InvalidRequest:
        return QStringLiteral("invalid_request");
    case Code::NotFound:
        return QStringLiteral("not_found");
    case Code::RevisionConflict:
        return QStringLiteral("revision_conflict");
    case Code::DuplicateCode:
        return QStringLiteral("duplicate_code");
    case Code::Database:
        return QStringLiteral("database_error");
    }
    return QStringLiteral("catalog_error");
}

StatusCode errorStatus(manage::data::CatalogErrorCode code) {
    using Code = manage::data::CatalogErrorCode;
    switch (code) {
    case Code::InvalidRequest:
        return StatusCode::BadRequest;
    case Code::NotFound:
        return StatusCode::NotFound;
    case Code::RevisionConflict:
    case Code::DuplicateCode:
        return StatusCode::Conflict;
    case Code::Database:
        return StatusCode::InternalServerError;
    }
    return StatusCode::InternalServerError;
}

QHttpServerResponse errorResponse(const manage::data::CatalogError& error) {
    QJsonObject object{
        {QStringLiteral("error"), errorCode(error.code)},
        {QStringLiteral("message"), error.message},
    };
    if (!error.field.isEmpty()) {
        object.insert(QStringLiteral("field"), error.field);
    }
    return QHttpServerResponse(object, errorStatus(error.code));
}

QHttpServerResponse invalidJsonResponse(const QString& message) {
    return QHttpServerResponse(
        QJsonObject{
            {QStringLiteral("error"), QStringLiteral("invalid_json")},
            {QStringLiteral("message"), message},
        },
        StatusCode::BadRequest
    );
}

std::optional<QJsonObject> requestObject(
    const QHttpServerRequest& request,
    QHttpServerResponse* failure
) {
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(request.body(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        *failure = invalidJsonResponse(
            QStringLiteral("request body must be a JSON object")
        );
        return std::nullopt;
    }
    return document.object();
}

bool readInteger(
    const QJsonObject& object,
    const QString& key,
    qint64* result,
    bool required,
    qint64 defaultValue = 0
) {
    if (!object.contains(key)) {
        if (required) {
            return false;
        }
        *result = defaultValue;
        return true;
    }
    const auto value = object.value(key);
    if (!value.isDouble()) {
        return false;
    }
    const auto number = value.toDouble();
    if (!std::isfinite(number) || std::trunc(number) != number ||
        number < -static_cast<double>(kMaxSafeJsonInteger) ||
        number > static_cast<double>(kMaxSafeJsonInteger)) {
        return false;
    }
    *result = static_cast<qint64>(number);
    return true;
}

bool readString(
    const QJsonObject& object,
    const QString& key,
    QString* result,
    bool required
) {
    if (!object.contains(key)) {
        if (required) {
            return false;
        }
        result->clear();
        return true;
    }
    if (!object.value(key).isString()) {
        return false;
    }
    *result = object.value(key).toString();
    return true;
}

bool readBool(
    const QJsonObject& object,
    const QString& key,
    bool* result,
    bool required,
    bool defaultValue = false
) {
    if (!object.contains(key)) {
        if (required) {
            return false;
        }
        *result = defaultValue;
        return true;
    }
    if (!object.value(key).isBool()) {
        return false;
    }
    *result = object.value(key).toBool();
    return true;
}

std::optional<manage::data::PageQuery> pageQuery(
    const QHttpServerRequest& request,
    bool allowEnabled,
    QHttpServerResponse* failure
) {
    manage::data::PageQuery result;
    const auto query = request.query();
    bool pageOk = true;
    bool pageSizeOk = true;
    if (query.hasQueryItem(QStringLiteral("page"))) {
        result.page = query.queryItemValue(QStringLiteral("page")).toInt(&pageOk);
    }
    if (query.hasQueryItem(QStringLiteral("pageSize"))) {
        result.pageSize = query.queryItemValue(QStringLiteral("pageSize")).toInt(&pageSizeOk);
    }
    result.search = query.queryItemValue(QStringLiteral("search"));
    if (!pageOk || !pageSizeOk) {
        *failure = invalidJsonResponse(
            QStringLiteral("page and pageSize must be integers")
        );
        return std::nullopt;
    }
    if (allowEnabled && query.hasQueryItem(QStringLiteral("enabled"))) {
        const auto enabled = query.queryItemValue(QStringLiteral("enabled")).toLower();
        if (enabled == QStringLiteral("true") || enabled == QStringLiteral("1")) {
            result.enabled = true;
        } else if (enabled == QStringLiteral("false") || enabled == QStringLiteral("0")) {
            result.enabled = false;
        } else {
            *failure = invalidJsonResponse(
                QStringLiteral("enabled must be true or false")
            );
            return std::nullopt;
        }
    }
    return result;
}

QJsonObject materialJson(const manage::data::Material& material) {
    return {
        {QStringLiteral("id"), material.id},
        {QStringLiteral("code"), material.code},
        {QStringLiteral("name"), material.name},
        {QStringLiteral("specification"), material.specification},
        {QStringLiteral("unit"), material.unit},
        {QStringLiteral("category"), material.category},
        {QStringLiteral("isCopperBased"), material.isCopperBased},
        {QStringLiteral("currentUnitPriceCents"), material.currentUnitPriceCents},
        {QStringLiteral("isEnabled"), material.isEnabled},
        {QStringLiteral("revision"), static_cast<qint64>(material.revision)},
        {QStringLiteral("createdAt"), material.createdAt.toUTC().toString(Qt::ISODateWithMs)},
        {QStringLiteral("updatedAt"), material.updatedAt.toUTC().toString(Qt::ISODateWithMs)},
    };
}

QJsonObject customerJson(const manage::data::Customer& customer) {
    return {
        {QStringLiteral("id"), customer.id},
        {QStringLiteral("name"), customer.name},
        {QStringLiteral("contactName"), customer.contactName},
        {QStringLiteral("phone"), customer.phone},
        {QStringLiteral("address"), customer.address},
        {QStringLiteral("notes"), customer.notes},
        {QStringLiteral("revision"), static_cast<qint64>(customer.revision)},
        {QStringLiteral("createdAt"), customer.createdAt.toUTC().toString(Qt::ISODateWithMs)},
        {QStringLiteral("updatedAt"), customer.updatedAt.toUTC().toString(Qt::ISODateWithMs)},
    };
}

QJsonObject materialSupplierJson(const manage::data::MaterialSupplier& supplier) {
    return {
        {QStringLiteral("id"), supplier.id},
        {QStringLiteral("materialId"), supplier.materialId},
        {QStringLiteral("supplierName"), supplier.supplierName},
        {QStringLiteral("contactName"), supplier.contactName},
        {QStringLiteral("phone"), supplier.phone},
        {QStringLiteral("isDefault"), supplier.isDefault},
        {QStringLiteral("isEnabled"), supplier.isEnabled},
        {QStringLiteral("leadDays"), supplier.leadDays},
        {QStringLiteral("revision"), static_cast<qint64>(supplier.revision)},
        {QStringLiteral("createdAt"), supplier.createdAt.toUTC().toString(Qt::ISODateWithMs)},
        {QStringLiteral("updatedAt"), supplier.updatedAt.toUTC().toString(Qt::ISODateWithMs)},
    };
}

QJsonObject materialPriceJson(const manage::data::MaterialPrice& price) {
    QJsonObject object{
        {QStringLiteral("id"), price.id},
        {QStringLiteral("supplierId"), price.supplierId},
        {QStringLiteral("unitPriceCents"), price.unitPriceCents},
        {QStringLiteral("isDefault"), price.isDefault},
        {QStringLiteral("isEnabled"), price.isEnabled},
        {QStringLiteral("revision"), static_cast<qint64>(price.revision)},
        {QStringLiteral("createdAt"), price.createdAt.toUTC().toString(Qt::ISODateWithMs)},
        {QStringLiteral("updatedAt"), price.updatedAt.toUTC().toString(Qt::ISODateWithMs)},
    };
    if (price.copperPriceCents.has_value()) {
        object.insert(
            QStringLiteral("copperPriceCents"),
            static_cast<qint64>(*price.copperPriceCents)
        );
    } else {
        object.insert(QStringLiteral("copperPriceCents"), QJsonValue::Null);
    }
    return object;
}

// 整包提交响应：物料 + 全部供应商 + 全部价格（按 supplierId 分组到所属供应商下）。
QJsonObject bundleJson(const manage::data::MaterialBundleResult& result) {
    QJsonArray suppliers;
    for (const auto& supplier : result.suppliers) {
        QJsonArray prices;
        for (const auto& price : result.prices) {
            if (price.supplierId == supplier.id) {
                prices.append(materialPriceJson(price));
            }
        }
        auto supplierObject = materialSupplierJson(supplier);
        supplierObject.insert(QStringLiteral("prices"), prices);
        suppliers.append(supplierObject);
    }
    QJsonObject object = materialJson(result.material);
    object.insert(QStringLiteral("suppliers"), suppliers);
    return object;
}

template <typename T, typename ToJson>
QHttpServerResponse pageResponse(
    const manage::data::CatalogResult<manage::data::Page<T>>& result,
    ToJson toJson
) {
    if (!result.ok()) {
        return errorResponse(*result.error);
    }
    QJsonArray items;
    for (const auto& item : result.value->items) {
        items.append(toJson(item));
    }
    const auto& page = *result.value;
    const auto totalPages = page.total == 0
                                ? 0
                                : (page.total + page.pageSize - 1) / page.pageSize;
    return QHttpServerResponse(QJsonObject{
        {QStringLiteral("items"), items},
        {QStringLiteral("page"), page.page},
        {QStringLiteral("pageSize"), page.pageSize},
        {QStringLiteral("total"), page.total},
        {QStringLiteral("totalPages"), totalPages},
    });
}

std::optional<manage::data::MaterialDraft> materialDraft(
    const QJsonObject& object,
    QHttpServerResponse* failure
) {
    manage::data::MaterialDraft draft;
    if (!readString(object, QStringLiteral("code"), &draft.code, true) ||
        !readString(object, QStringLiteral("name"), &draft.name, true) ||
        !readString(object, QStringLiteral("specification"), &draft.specification, false) ||
        !readString(object, QStringLiteral("unit"), &draft.unit, true) ||
        !readString(object, QStringLiteral("category"), &draft.category, false) ||
        !readBool(object, QStringLiteral("isCopperBased"), &draft.isCopperBased, false, false) ||
        !readInteger(
            object,
            QStringLiteral("currentUnitPriceCents"),
            &draft.currentUnitPriceCents,
            true
        ) ||
        !readBool(object, QStringLiteral("isEnabled"), &draft.isEnabled, false, true)) {
        *failure = invalidJsonResponse(
            QStringLiteral("material fields have invalid or missing types")
        );
        return std::nullopt;
    }
    return draft;
}

std::optional<manage::data::MaterialSupplierDraft> materialSupplierDraft(
    const QJsonObject& object,
    QHttpServerResponse* failure
) {
    manage::data::MaterialSupplierDraft draft;
    if (!readString(object, QStringLiteral("supplierName"), &draft.supplierName, true) ||
        !readString(object, QStringLiteral("contactName"), &draft.contactName, false) ||
        !readString(object, QStringLiteral("phone"), &draft.phone, false) ||
        !readBool(object, QStringLiteral("isDefault"), &draft.isDefault, false, false) ||
        !readBool(object, QStringLiteral("isEnabled"), &draft.isEnabled, false, true)) {
        *failure = invalidJsonResponse(
            QStringLiteral("supplier fields have invalid or missing types")
        );
        return std::nullopt;
    }
    qint64 leadDays = 0;
    if (!readInteger(object, QStringLiteral("leadDays"), &leadDays, false, 0) ||
        leadDays > std::numeric_limits<int>::max()) {
        *failure = invalidJsonResponse(
            QStringLiteral("leadDays must be a non-negative safe integer")
        );
        return std::nullopt;
    }
    draft.leadDays = static_cast<int>(leadDays);
    return draft;
}

std::optional<manage::data::MaterialPriceDraft> materialPriceDraft(
    const QJsonObject& object,
    QHttpServerResponse* failure
) {
    manage::data::MaterialPriceDraft draft;
    if (!readInteger(
            object,
            QStringLiteral("unitPriceCents"),
            &draft.unitPriceCents,
            true
        ) ||
        !readBool(object, QStringLiteral("isDefault"), &draft.isDefault, false, false) ||
        !readBool(object, QStringLiteral("isEnabled"), &draft.isEnabled, false, true)) {
        *failure = invalidJsonResponse(
            QStringLiteral("price fields have invalid or missing types")
        );
        return std::nullopt;
    }
    if (object.contains(QStringLiteral("copperPriceCents"))) {
        if (object.value(QStringLiteral("copperPriceCents")).isNull()) {
            draft.copperPriceCents.reset();
        } else {
            qint64 copper = 0;
            if (!readInteger(
                    object,
                    QStringLiteral("copperPriceCents"),
                    &copper,
                    true
                )) {
                *failure = invalidJsonResponse(
                    QStringLiteral("copperPriceCents must be a non-negative integer or null")
                );
                return std::nullopt;
            }
            draft.copperPriceCents = copper;
        }
    }
    return draft;
}

std::optional<manage::data::CustomerDraft> customerDraft(
    const QJsonObject& object,
    QHttpServerResponse* failure
) {
    manage::data::CustomerDraft draft;
    if (!readString(object, QStringLiteral("name"), &draft.name, true) ||
        !readString(object, QStringLiteral("contactName"), &draft.contactName, false) ||
        !readString(object, QStringLiteral("phone"), &draft.phone, false) ||
        !readString(object, QStringLiteral("address"), &draft.address, false) ||
        !readString(object, QStringLiteral("notes"), &draft.notes, false)) {
        *failure = invalidJsonResponse(
            QStringLiteral("customer fields have invalid or missing types")
        );
        return std::nullopt;
    }
    return draft;
}

std::optional<std::uint32_t> revision(
    const QJsonObject& object,
    QHttpServerResponse* failure
) {
    qint64 value = 0;
    if (!readInteger(object, QStringLiteral("revision"), &value, true) ||
        value <= 0 || value > std::numeric_limits<std::uint32_t>::max()) {
        *failure = invalidJsonResponse(
            QStringLiteral("revision must be a positive 32-bit integer")
        );
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(value);
}

// 物料库三级向导整包解析：{ material, suppliers: [{ supplier, prices: [...] }] }
std::optional<manage::data::MaterialBundleDraft> materialBundleDraft(
    const QJsonObject& object,
    QHttpServerResponse* failure
) {
    manage::data::MaterialBundleDraft bundle;
    const auto materialValue = object.value(QStringLiteral("material"));
    const auto suppliersValue = object.value(QStringLiteral("suppliers"));
    if (!materialValue.isObject() || !suppliersValue.isArray()) {
        *failure = invalidJsonResponse(
            QStringLiteral("bundle requires material object and suppliers array")
        );
        return std::nullopt;
    }

    const auto parsedMaterial = materialDraft(materialValue.toObject(), failure);
    if (!parsedMaterial.has_value()) {
        return std::nullopt;
    }
    bundle.material = *parsedMaterial;

    const auto suppliers = suppliersValue.toArray();
    if (suppliers.size() > 500) {
        *failure = invalidJsonResponse(
            QStringLiteral("suppliers array exceeds the 500 item limit")
        );
        return std::nullopt;
    }
    bundle.suppliers.reserve(static_cast<std::size_t>(suppliers.size()));
    for (const auto& value : suppliers) {
        if (!value.isObject()) {
            *failure = invalidJsonResponse(
                QStringLiteral("each supplier must be an object")
            );
            return std::nullopt;
        }
        const auto supplierObject = value.toObject();
        manage::data::MaterialBundleSupplierDraft entry;
        const auto parsedSupplier =
            materialSupplierDraft(supplierObject, failure);
        if (!parsedSupplier.has_value()) {
            return std::nullopt;
        }
        entry.supplier = *parsedSupplier;

        const auto pricesValue = supplierObject.value(QStringLiteral("prices"));
        if (!pricesValue.isArray()) {
            *failure = invalidJsonResponse(
                QStringLiteral("each supplier requires a prices array")
            );
            return std::nullopt;
        }
        const auto prices = pricesValue.toArray();
        if (prices.size() > 100) {
            *failure = invalidJsonResponse(
                QStringLiteral("prices array exceeds the 100 item limit")
            );
            return std::nullopt;
        }
        entry.prices.reserve(static_cast<std::size_t>(prices.size()));
        for (const auto& priceValue : prices) {
            if (!priceValue.isObject()) {
                *failure = invalidJsonResponse(
                    QStringLiteral("each price must be an object")
                );
                return std::nullopt;
            }
            const auto parsedPrice =
                materialPriceDraft(priceValue.toObject(), failure);
            if (!parsedPrice.has_value()) {
                return std::nullopt;
            }
            entry.prices.push_back(*parsedPrice);
        }
        bundle.suppliers.push_back(std::move(entry));
    }
    return bundle;
}

} // namespace

void registerCatalogRoutes(
    QHttpServer& server,
    const std::shared_ptr<manage::data::CatalogService>& service,
    const std::shared_ptr<manage::auth::AuthService>& authService
) {
    server.route(
        QStringLiteral("/api/v1/materials"),
        QHttpServerRequest::Method::Get,
        [service, authService](const QHttpServerRequest& request) {
            if (auto failure = HttpAuthorization::require(
                    request,
                    authService,
                    {
                        manage::auth::UserRole::Admin,
                        manage::auth::UserRole::Quoter,
                        manage::auth::UserRole::Viewer,
                    }
                )) {
                return std::move(*failure);
            }
            QHttpServerResponse failure(StatusCode::BadRequest);
            const auto query = pageQuery(request, true, &failure);
            if (!query.has_value()) {
                return failure;
            }
            return pageResponse(service->listMaterials(*query), materialJson);
        }
    );
    server.route(
        QStringLiteral("/api/v1/materials/<arg>"),
        QHttpServerRequest::Method::Get,
        [service, authService](
            qint64 id,
            const QHttpServerRequest& request
        ) {
            if (auto failure = HttpAuthorization::require(
                    request,
                    authService,
                    {
                        manage::auth::UserRole::Admin,
                        manage::auth::UserRole::Quoter,
                        manage::auth::UserRole::Viewer,
                    }
                )) {
                return std::move(*failure);
            }
            const auto result = service->getMaterial(id);
            return result.ok() ? QHttpServerResponse(materialJson(*result.value))
                               : errorResponse(*result.error);
        }
    );
    server.route(
        QStringLiteral("/api/v1/materials"),
        QHttpServerRequest::Method::Post,
        [service, authService](const QHttpServerRequest& request) {
            if (auto failure = HttpAuthorization::require(
                    request,
                    authService,
                    {manage::auth::UserRole::Admin}
                )) {
                return std::move(*failure);
            }
            QHttpServerResponse failure(StatusCode::BadRequest);
            const auto object = requestObject(request, &failure);
            if (!object.has_value()) {
                return failure;
            }
            const auto draft = materialDraft(*object, &failure);
            if (!draft.has_value()) {
                return failure;
            }
            const auto result = service->createMaterial(*draft);
            return result.ok()
                       ? QHttpServerResponse(materialJson(*result.value), StatusCode::Created)
                       : errorResponse(*result.error);
        }
    );
    // 物料库三级向导整包提交：物料 + 供应商 + 价格，事务原子写入。
    server.route(
        QStringLiteral("/api/v1/materials/bundle"),
        QHttpServerRequest::Method::Post,
        [service, authService](const QHttpServerRequest& request) {
            if (auto failure = HttpAuthorization::require(
                    request,
                    authService,
                    {manage::auth::UserRole::Admin}
                )) {
                return std::move(*failure);
            }
            if (service == nullptr) {
                return invalidJsonResponse(
                    QStringLiteral("catalog service is unavailable")
                );
            }
            QHttpServerResponse failure(StatusCode::BadRequest);
            const auto object = requestObject(request, &failure);
            if (!object.has_value()) {
                return failure;
            }
            const auto bundle = materialBundleDraft(*object, &failure);
            if (!bundle.has_value()) {
                return failure;
            }
            const auto result = service->createMaterialBundle(*bundle);
            return result.ok()
                       ? QHttpServerResponse(bundleJson(*result.value), StatusCode::Created)
                       : errorResponse(*result.error);
        }
    );
    server.route(
        QStringLiteral("/api/v1/materials/<arg>"),
        QHttpServerRequest::Method::Put,
        [service, authService](qint64 id, const QHttpServerRequest& request) {
            if (auto failure = HttpAuthorization::require(
                    request,
                    authService,
                    {manage::auth::UserRole::Admin}
                )) {
                return std::move(*failure);
            }
            QHttpServerResponse failure(StatusCode::BadRequest);
            const auto object = requestObject(request, &failure);
            if (!object.has_value()) {
                return failure;
            }
            const auto draft = materialDraft(*object, &failure);
            const auto expectedRevision = revision(*object, &failure);
            if (!draft.has_value() || !expectedRevision.has_value()) {
                return failure;
            }
            const auto result = service->updateMaterial(id, *expectedRevision, *draft);
            return result.ok() ? QHttpServerResponse(materialJson(*result.value))
                               : errorResponse(*result.error);
        }
    );
    server.route(
        QStringLiteral("/api/v1/materials/<arg>/enabled"),
        QHttpServerRequest::Method::Patch,
        [service, authService](qint64 id, const QHttpServerRequest& request) {
            if (auto failure = HttpAuthorization::require(
                    request,
                    authService,
                    {manage::auth::UserRole::Admin}
                )) {
                return std::move(*failure);
            }
            QHttpServerResponse failure(StatusCode::BadRequest);
            const auto object = requestObject(request, &failure);
            if (!object.has_value()) {
                return failure;
            }
            bool enabled = false;
            const auto expectedRevision = revision(*object, &failure);
            if (!expectedRevision.has_value() ||
                !readBool(*object, QStringLiteral("isEnabled"), &enabled, true)) {
                return invalidJsonResponse(
                    QStringLiteral("isEnabled must be a boolean and revision is required")
                );
            }
            const auto result = service->setMaterialEnabled(id, *expectedRevision, enabled);
            return result.ok() ? QHttpServerResponse(materialJson(*result.value))
                               : errorResponse(*result.error);
        }
    );

    server.route(
        QStringLiteral("/api/v1/customers"),
        QHttpServerRequest::Method::Get,
        [service, authService](const QHttpServerRequest& request) {
            if (auto failure = HttpAuthorization::require(
                    request,
                    authService,
                    {
                        manage::auth::UserRole::Admin,
                        manage::auth::UserRole::Quoter,
                        manage::auth::UserRole::Viewer,
                    }
                )) {
                return std::move(*failure);
            }
            QHttpServerResponse failure(StatusCode::BadRequest);
            const auto query = pageQuery(request, false, &failure);
            if (!query.has_value()) {
                return failure;
            }
            return pageResponse(service->listCustomers(*query), customerJson);
        }
    );
    server.route(
        QStringLiteral("/api/v1/customers/<arg>"),
        QHttpServerRequest::Method::Get,
        [service, authService](
            qint64 id,
            const QHttpServerRequest& request
        ) {
            if (auto failure = HttpAuthorization::require(
                    request,
                    authService,
                    {
                        manage::auth::UserRole::Admin,
                        manage::auth::UserRole::Quoter,
                        manage::auth::UserRole::Viewer,
                    }
                )) {
                return std::move(*failure);
            }
            const auto result = service->getCustomer(id);
            return result.ok() ? QHttpServerResponse(customerJson(*result.value))
                               : errorResponse(*result.error);
        }
    );
    server.route(
        QStringLiteral("/api/v1/customers"),
        QHttpServerRequest::Method::Post,
        [service, authService](const QHttpServerRequest& request) {
            if (auto failure = HttpAuthorization::require(
                    request,
                    authService,
                    {manage::auth::UserRole::Admin}
                )) {
                return std::move(*failure);
            }
            QHttpServerResponse failure(StatusCode::BadRequest);
            const auto object = requestObject(request, &failure);
            if (!object.has_value()) {
                return failure;
            }
            const auto draft = customerDraft(*object, &failure);
            if (!draft.has_value()) {
                return failure;
            }
            const auto result = service->createCustomer(*draft);
            return result.ok()
                       ? QHttpServerResponse(customerJson(*result.value), StatusCode::Created)
                       : errorResponse(*result.error);
        }
    );
    server.route(
        QStringLiteral("/api/v1/customers/<arg>"),
        QHttpServerRequest::Method::Put,
        [service, authService](qint64 id, const QHttpServerRequest& request) {
            if (auto failure = HttpAuthorization::require(
                    request,
                    authService,
                    {manage::auth::UserRole::Admin}
                )) {
                return std::move(*failure);
            }
            QHttpServerResponse failure(StatusCode::BadRequest);
            const auto object = requestObject(request, &failure);
            if (!object.has_value()) {
                return failure;
            }
            const auto draft = customerDraft(*object, &failure);
            const auto expectedRevision = revision(*object, &failure);
            if (!draft.has_value() || !expectedRevision.has_value()) {
                return failure;
            }
            const auto result = service->updateCustomer(id, *expectedRevision, *draft);
            return result.ok() ? QHttpServerResponse(customerJson(*result.value))
                               : errorResponse(*result.error);
        }
    );

    // 物料供应商分支
    server.route(
        QStringLiteral("/api/v1/materials/<arg>/suppliers"),
        QHttpServerRequest::Method::Get,
        [service, authService](
            qint64 materialId,
            const QHttpServerRequest& request
        ) {
            if (auto failure = HttpAuthorization::require(
                    request,
                    authService,
                    {
                        manage::auth::UserRole::Admin,
                        manage::auth::UserRole::Quoter,
                        manage::auth::UserRole::Viewer,
                    }
                )) {
                return std::move(*failure);
            }
            QHttpServerResponse failure(StatusCode::BadRequest);
            const auto query = pageQuery(request, false, &failure);
            if (!query.has_value()) {
                return failure;
            }
            return pageResponse(
                service->listMaterialSuppliers(materialId, *query),
                materialSupplierJson
            );
        }
    );
    server.route(
        QStringLiteral("/api/v1/materials/<arg>/suppliers"),
        QHttpServerRequest::Method::Post,
        [service, authService](
            qint64 materialId,
            const QHttpServerRequest& request
        ) {
            if (auto failure = HttpAuthorization::require(
                    request,
                    authService,
                    {manage::auth::UserRole::Admin}
                )) {
                return std::move(*failure);
            }
            QHttpServerResponse failure(StatusCode::BadRequest);
            const auto object = requestObject(request, &failure);
            if (!object.has_value()) {
                return failure;
            }
            const auto draft = materialSupplierDraft(*object, &failure);
            if (!draft.has_value()) {
                return failure;
            }
            const auto result = service->createMaterialSupplier(materialId, *draft);
            return result.ok()
                       ? QHttpServerResponse(
                             materialSupplierJson(*result.value),
                             StatusCode::Created
                         )
                       : errorResponse(*result.error);
        }
    );
    server.route(
        QStringLiteral("/api/v1/materials/<arg>/suppliers/<arg>"),
        QHttpServerRequest::Method::Get,
        [service, authService](
            qint64 materialId,
            qint64 supplierId,
            const QHttpServerRequest& request
        ) {
            if (auto failure = HttpAuthorization::require(
                    request,
                    authService,
                    {
                        manage::auth::UserRole::Admin,
                        manage::auth::UserRole::Quoter,
                        manage::auth::UserRole::Viewer,
                    }
                )) {
                return std::move(*failure);
            }
            Q_UNUSED(materialId);
            const auto result = service->getMaterialSupplier(supplierId);
            return result.ok()
                       ? QHttpServerResponse(materialSupplierJson(*result.value))
                       : errorResponse(*result.error);
        }
    );
    server.route(
        QStringLiteral("/api/v1/materials/<arg>/suppliers/<arg>"),
        QHttpServerRequest::Method::Put,
        [service, authService](
            qint64 materialId,
            qint64 supplierId,
            const QHttpServerRequest& request
        ) {
            if (auto failure = HttpAuthorization::require(
                    request,
                    authService,
                    {manage::auth::UserRole::Admin}
                )) {
                return std::move(*failure);
            }
            Q_UNUSED(materialId);
            QHttpServerResponse failure(StatusCode::BadRequest);
            const auto object = requestObject(request, &failure);
            if (!object.has_value()) {
                return failure;
            }
            const auto draft = materialSupplierDraft(*object, &failure);
            const auto expectedRevision = revision(*object, &failure);
            if (!draft.has_value() || !expectedRevision.has_value()) {
                return failure;
            }
            const auto result =
                service->updateMaterialSupplier(supplierId, *expectedRevision, *draft);
            return result.ok()
                       ? QHttpServerResponse(materialSupplierJson(*result.value))
                       : errorResponse(*result.error);
        }
    );
    server.route(
        QStringLiteral("/api/v1/materials/<arg>/suppliers/<arg>/enabled"),
        QHttpServerRequest::Method::Patch,
        [service, authService](
            qint64 materialId,
            qint64 supplierId,
            const QHttpServerRequest& request
        ) {
            if (auto failure = HttpAuthorization::require(
                    request,
                    authService,
                    {manage::auth::UserRole::Admin}
                )) {
                return std::move(*failure);
            }
            Q_UNUSED(materialId);
            QHttpServerResponse failure(StatusCode::BadRequest);
            const auto object = requestObject(request, &failure);
            if (!object.has_value()) {
                return failure;
            }
            bool enabled = false;
            const auto expectedRevision = revision(*object, &failure);
            if (!expectedRevision.has_value() ||
                !readBool(*object, QStringLiteral("isEnabled"), &enabled, true)) {
                return invalidJsonResponse(
                    QStringLiteral("isEnabled must be a boolean and revision is required")
                );
            }
            const auto result = service->setMaterialSupplierEnabled(
                supplierId,
                *expectedRevision,
                enabled
            );
            return result.ok()
                       ? QHttpServerResponse(materialSupplierJson(*result.value))
                       : errorResponse(*result.error);
        }
    );

    // 供应商价格分支（电线类按铜价区分）
    server.route(
        QStringLiteral("/api/v1/suppliers/<arg>/prices"),
        QHttpServerRequest::Method::Get,
        [service, authService](
            qint64 supplierId,
            const QHttpServerRequest& request
        ) {
            if (auto failure = HttpAuthorization::require(
                    request,
                    authService,
                    {
                        manage::auth::UserRole::Admin,
                        manage::auth::UserRole::Quoter,
                        manage::auth::UserRole::Viewer,
                    }
                )) {
                return std::move(*failure);
            }
            QHttpServerResponse failure(StatusCode::BadRequest);
            const auto query = pageQuery(request, false, &failure);
            if (!query.has_value()) {
                return failure;
            }
            return pageResponse(
                service->listMaterialPrices(supplierId, *query),
                materialPriceJson
            );
        }
    );
    server.route(
        QStringLiteral("/api/v1/suppliers/<arg>/prices"),
        QHttpServerRequest::Method::Post,
        [service, authService](
            qint64 supplierId,
            const QHttpServerRequest& request
        ) {
            if (auto failure = HttpAuthorization::require(
                    request,
                    authService,
                    {manage::auth::UserRole::Admin}
                )) {
                return std::move(*failure);
            }
            QHttpServerResponse failure(StatusCode::BadRequest);
            const auto object = requestObject(request, &failure);
            if (!object.has_value()) {
                return failure;
            }
            const auto draft = materialPriceDraft(*object, &failure);
            if (!draft.has_value()) {
                return failure;
            }
            const auto result = service->createMaterialPrice(supplierId, *draft);
            return result.ok()
                       ? QHttpServerResponse(
                             materialPriceJson(*result.value),
                             StatusCode::Created
                         )
                       : errorResponse(*result.error);
        }
    );
    server.route(
        QStringLiteral("/api/v1/suppliers/<arg>/prices/<arg>"),
        QHttpServerRequest::Method::Get,
        [service, authService](
            qint64 supplierId,
            qint64 priceId,
            const QHttpServerRequest& request
        ) {
            if (auto failure = HttpAuthorization::require(
                    request,
                    authService,
                    {
                        manage::auth::UserRole::Admin,
                        manage::auth::UserRole::Quoter,
                        manage::auth::UserRole::Viewer,
                    }
                )) {
                return std::move(*failure);
            }
            Q_UNUSED(supplierId);
            const auto result = service->getMaterialPrice(priceId);
            return result.ok() ? QHttpServerResponse(materialPriceJson(*result.value))
                               : errorResponse(*result.error);
        }
    );
    server.route(
        QStringLiteral("/api/v1/suppliers/<arg>/prices/<arg>"),
        QHttpServerRequest::Method::Put,
        [service, authService](
            qint64 supplierId,
            qint64 priceId,
            const QHttpServerRequest& request
        ) {
            if (auto failure = HttpAuthorization::require(
                    request,
                    authService,
                    {manage::auth::UserRole::Admin}
                )) {
                return std::move(*failure);
            }
            Q_UNUSED(supplierId);
            QHttpServerResponse failure(StatusCode::BadRequest);
            const auto object = requestObject(request, &failure);
            if (!object.has_value()) {
                return failure;
            }
            const auto draft = materialPriceDraft(*object, &failure);
            const auto expectedRevision = revision(*object, &failure);
            if (!draft.has_value() || !expectedRevision.has_value()) {
                return failure;
            }
            const auto result =
                service->updateMaterialPrice(priceId, *expectedRevision, *draft);
            return result.ok() ? QHttpServerResponse(materialPriceJson(*result.value))
                               : errorResponse(*result.error);
        }
    );
    server.route(
        QStringLiteral("/api/v1/suppliers/<arg>/prices/<arg>/enabled"),
        QHttpServerRequest::Method::Patch,
        [service, authService](
            qint64 supplierId,
            qint64 priceId,
            const QHttpServerRequest& request
        ) {
            if (auto failure = HttpAuthorization::require(
                    request,
                    authService,
                    {manage::auth::UserRole::Admin}
                )) {
                return std::move(*failure);
            }
            Q_UNUSED(supplierId);
            QHttpServerResponse failure(StatusCode::BadRequest);
            const auto object = requestObject(request, &failure);
            if (!object.has_value()) {
                return failure;
            }
            bool enabled = false;
            const auto expectedRevision = revision(*object, &failure);
            if (!expectedRevision.has_value() ||
                !readBool(*object, QStringLiteral("isEnabled"), &enabled, true)) {
                return invalidJsonResponse(
                    QStringLiteral("isEnabled must be a boolean and revision is required")
                );
            }
            const auto result = service->setMaterialPriceEnabled(
                priceId,
                *expectedRevision,
                enabled
            );
            return result.ok() ? QHttpServerResponse(materialPriceJson(*result.value))
                               : errorResponse(*result.error);
        }
    );
}

} // namespace manage::server
