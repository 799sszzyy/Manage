#include "manage/server/bom_routes.h"

#include "manage/server/http_authorization.h"

#include "manage/data/bom_service.h"

#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponder>
#include <QHttpServerResponse>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QUrlQuery>

#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace manage::server {
namespace {

using StatusCode = QHttpServerResponder::StatusCode;
constexpr qint64 kMaxSafeJsonInteger = 9'007'199'254'740'991;

QHttpServerResponse errorResponse(
    const QString& code,
    const QString& message,
    StatusCode status
) {
    return QHttpServerResponse(
        QJsonObject{
            {QStringLiteral("error"), code},
            {QStringLiteral("message"), message},
        },
        status
    );
}

QHttpServerResponse serviceUnavailable() {
    return errorResponse(
        QStringLiteral("database_unavailable"),
        QStringLiteral("BOM storage is unavailable"),
        StatusCode::ServiceUnavailable
    );
}

QHttpServerResponse serviceError(
    manage::data::BomErrorCode error,
    const QString& message
) {
    using ErrorCode = manage::data::BomErrorCode;
    switch (error) {
    case ErrorCode::Validation:
        return errorResponse(QStringLiteral("invalid_request"), message,
                             StatusCode::BadRequest);
    case ErrorCode::NotFound:
        return errorResponse(QStringLiteral("not_found"), message,
                             StatusCode::NotFound);
    case ErrorCode::Conflict:
        return errorResponse(QStringLiteral("revision_conflict"), message,
                             StatusCode::Conflict);
    case ErrorCode::DuplicateCode:
        return errorResponse(QStringLiteral("duplicate_code"), message,
                             StatusCode::Conflict);
    case ErrorCode::Infrastructure:
        return errorResponse(QStringLiteral("database_error"), message,
                             StatusCode::InternalServerError);
    case ErrorCode::None:
        break;
    }
    return errorResponse(QStringLiteral("server_error"), message,
                         StatusCode::InternalServerError);
}

QJsonObject summaryJson(const manage::data::BomTemplateSummary& summary) {
    return QJsonObject{
        {QStringLiteral("id"), summary.id},
        {QStringLiteral("code"), summary.code},
        {QStringLiteral("name"), summary.name},
        {QStringLiteral("description"), summary.description},
        {QStringLiteral("isEnabled"), summary.isEnabled},
        {QStringLiteral("revision"), summary.revision},
    };
}

QJsonObject templateJson(const manage::data::BomTemplate& bom) {
    auto object = summaryJson(bom.summary);
    QJsonArray items;
    for (const auto& item : bom.items) {
        items.append(QJsonObject{
            {QStringLiteral("id"), item.id},
            {QStringLiteral("lineNo"), item.lineNo},
            {QStringLiteral("materialId"), item.materialId},
            {QStringLiteral("materialCode"), item.materialCode},
            {QStringLiteral("materialName"), item.materialName},
            {QStringLiteral("materialSpecification"), item.materialSpecification},
            {QStringLiteral("materialUnit"), item.materialUnit},
            {QStringLiteral("quantityMicros"), item.quantityMicros},
            {QStringLiteral("notes"), item.notes},
        });
    }
    object.insert(QStringLiteral("items"), items);
    return object;
}

std::optional<QJsonObject> requestObject(
    const QHttpServerRequest& request,
    QHttpServerResponse& error
) {
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(request.body(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        error = errorResponse(
            QStringLiteral("invalid_json"),
            QStringLiteral("request body must be a JSON object"),
            StatusCode::BadRequest
        );
        return std::nullopt;
    }
    return document.object();
}

bool readInteger(const QJsonObject& object, const QString& key, qint64& value) {
    const auto jsonValue = object.value(key);
    if (!jsonValue.isDouble()) {
        return false;
    }
    const auto number = jsonValue.toDouble();
    if (!std::isfinite(number) || std::trunc(number) != number ||
        number < -static_cast<double>(kMaxSafeJsonInteger) ||
        number > static_cast<double>(kMaxSafeJsonInteger)) {
        return false;
    }
    value = static_cast<qint64>(number);
    return true;
}

bool readItems(
    const QJsonObject& object,
    bool required,
    std::vector<manage::data::BomItemInput>& result,
    QString& errorMessage
) {
    if (!object.contains(QStringLiteral("items"))) {
        if (!required) {
            result.clear();
            return true;
        }
        errorMessage = QStringLiteral("items must be an array");
        return false;
    }
    const auto itemsValue = object.value(QStringLiteral("items"));
    if (!itemsValue.isArray()) {
        errorMessage = QStringLiteral("items must be an array");
        return false;
    }

    const auto items = itemsValue.toArray();
    result.clear();
    result.reserve(static_cast<std::size_t>(items.size()));
    for (qsizetype index = 0; index < items.size(); ++index) {
        if (!items.at(index).isObject()) {
            errorMessage = QStringLiteral("items[%1] must be an object").arg(index);
            return false;
        }
        const auto item = items.at(index).toObject();
        qint64 lineNo = 0;
        qint64 materialId = 0;
        qint64 quantityMicros = 0;
        if (!readInteger(item, QStringLiteral("lineNo"), lineNo) ||
            !readInteger(item, QStringLiteral("materialId"), materialId) ||
            !readInteger(item, QStringLiteral("quantityMicros"), quantityMicros) ||
            lineNo < std::numeric_limits<int>::min() ||
            lineNo > std::numeric_limits<int>::max()) {
            errorMessage = QStringLiteral(
                "items[%1] lineNo, materialId and quantityMicros must be safe integers"
            ).arg(index);
            return false;
        }
        if (item.contains(QStringLiteral("notes")) &&
            !item.value(QStringLiteral("notes")).isString()) {
            errorMessage = QStringLiteral("items[%1].notes must be a string").arg(index);
            return false;
        }
        result.push_back({
            static_cast<int>(lineNo),
            materialId,
            quantityMicros,
            item.value(QStringLiteral("notes")).toString(),
        });
    }
    return true;
}

bool readString(
    const QJsonObject& object,
    const QString& key,
    QString& result,
    bool required
) {
    if (!object.contains(key)) {
        if (required) {
            return false;
        }
        result.clear();
        return true;
    }
    if (!object.value(key).isString()) {
        return false;
    }
    result = object.value(key).toString();
    return true;
}

QHttpServerResponse listBoms(
    manage::data::BomService* service,
    const QHttpServerRequest& request
) {
    if (service == nullptr) {
        return serviceUnavailable();
    }

    manage::data::BomSearchQuery query;
    const QUrlQuery urlQuery(request.url());
    bool ok = false;
    if (urlQuery.hasQueryItem(QStringLiteral("page"))) {
        query.page = urlQuery.queryItemValue(QStringLiteral("page")).toInt(&ok);
        if (!ok) {
            return errorResponse(QStringLiteral("invalid_request"),
                                 QStringLiteral("page must be an integer"),
                                 StatusCode::BadRequest);
        }
    }
    if (urlQuery.hasQueryItem(QStringLiteral("pageSize"))) {
        query.pageSize = urlQuery.queryItemValue(QStringLiteral("pageSize")).toInt(&ok);
        if (!ok) {
            return errorResponse(QStringLiteral("invalid_request"),
                                 QStringLiteral("pageSize must be an integer"),
                                 StatusCode::BadRequest);
        }
    }
    query.search = urlQuery.queryItemValue(QStringLiteral("search"));
    if (urlQuery.hasQueryItem(QStringLiteral("enabled"))) {
        const auto enabled = urlQuery.queryItemValue(QStringLiteral("enabled"));
        if (enabled == QStringLiteral("true")) {
            query.enabled = true;
        } else if (enabled == QStringLiteral("false")) {
            query.enabled = false;
        } else {
            return errorResponse(QStringLiteral("invalid_request"),
                                 QStringLiteral("enabled must be true or false"),
                                 StatusCode::BadRequest);
        }
    }

    const auto result = service->list(std::move(query));
    if (!result.ok()) {
        return serviceError(result.error, result.message);
    }
    QJsonArray items;
    for (const auto& item : result.value->items) {
        items.append(summaryJson(item));
    }
    return QHttpServerResponse(QJsonObject{
        {QStringLiteral("items"), items},
        {QStringLiteral("total"), result.value->total},
        {QStringLiteral("page"), result.value->page},
        {QStringLiteral("pageSize"), result.value->pageSize},
    });
}

QHttpServerResponse getBom(manage::data::BomService* service, qint64 id) {
    if (service == nullptr) {
        return serviceUnavailable();
    }
    const auto result = service->getById(id);
    return result.ok() ? QHttpServerResponse(templateJson(*result.value))
                       : serviceError(result.error, result.message);
}

QHttpServerResponse createBom(
    manage::data::BomService* service,
    const QHttpServerRequest& request
) {
    if (service == nullptr) {
        return serviceUnavailable();
    }
    QHttpServerResponse parseResponse(StatusCode::BadRequest);
    const auto object = requestObject(request, parseResponse);
    if (!object.has_value()) {
        return parseResponse;
    }

    manage::data::NewBomTemplate command;
    QString itemError;
    if (!readString(*object, QStringLiteral("code"), command.code, true) ||
        !readString(*object, QStringLiteral("name"), command.name, true) ||
        !readString(*object, QStringLiteral("description"), command.description, false) ||
        !readItems(*object, false, command.items, itemError)) {
        return errorResponse(
            QStringLiteral("invalid_request"),
            itemError.isEmpty()
                ? QStringLiteral("code and name must be strings")
                : itemError,
            StatusCode::BadRequest
        );
    }
    if (object->contains(QStringLiteral("isEnabled"))) {
        if (!object->value(QStringLiteral("isEnabled")).isBool()) {
            return errorResponse(QStringLiteral("invalid_request"),
                                 QStringLiteral("isEnabled must be a boolean"),
                                 StatusCode::BadRequest);
        }
        command.isEnabled = object->value(QStringLiteral("isEnabled")).toBool();
    }

    const auto result = service->create(std::move(command));
    return result.ok()
               ? QHttpServerResponse(templateJson(*result.value), StatusCode::Created)
               : serviceError(result.error, result.message);
}

QHttpServerResponse updateBom(
    manage::data::BomService* service,
    qint64 id,
    const QHttpServerRequest& request
) {
    if (service == nullptr) {
        return serviceUnavailable();
    }
    QHttpServerResponse parseResponse(StatusCode::BadRequest);
    const auto object = requestObject(request, parseResponse);
    if (!object.has_value()) {
        return parseResponse;
    }

    manage::data::UpdateBomTemplate command;
    command.id = id;
    qint64 revision = 0;
    if (!readString(*object, QStringLiteral("code"), command.code, true) ||
        !readString(*object, QStringLiteral("name"), command.name, true) ||
        !readString(*object, QStringLiteral("description"), command.description, false) ||
        !readInteger(*object, QStringLiteral("revision"), revision) ||
        revision < std::numeric_limits<int>::min() ||
        revision > std::numeric_limits<int>::max()) {
        return errorResponse(QStringLiteral("invalid_request"),
                             QStringLiteral("code, name and integer revision are required"),
                             StatusCode::BadRequest);
    }
    command.expectedRevision = static_cast<int>(revision);

    const auto result = service->update(std::move(command));
    return result.ok() ? QHttpServerResponse(templateJson(*result.value))
                       : serviceError(result.error, result.message);
}

QHttpServerResponse setBomEnabled(
    manage::data::BomService* service,
    qint64 id,
    const QHttpServerRequest& request
) {
    if (service == nullptr) {
        return serviceUnavailable();
    }
    QHttpServerResponse parseResponse(StatusCode::BadRequest);
    const auto object = requestObject(request, parseResponse);
    if (!object.has_value()) {
        return parseResponse;
    }

    qint64 revision = 0;
    if (!object->value(QStringLiteral("isEnabled")).isBool() ||
        !readInteger(*object, QStringLiteral("revision"), revision) ||
        revision < std::numeric_limits<int>::min() ||
        revision > std::numeric_limits<int>::max()) {
        return errorResponse(QStringLiteral("invalid_request"),
                             QStringLiteral("isEnabled and integer revision are required"),
                             StatusCode::BadRequest);
    }
    const manage::data::SetBomEnabled command{
        id,
        object->value(QStringLiteral("isEnabled")).toBool(),
        static_cast<int>(revision),
    };
    const auto result = service->setEnabled(command);
    return result.ok() ? QHttpServerResponse(templateJson(*result.value))
                       : serviceError(result.error, result.message);
}

QHttpServerResponse replaceBomItems(
    manage::data::BomService* service,
    qint64 id,
    const QHttpServerRequest& request
) {
    if (service == nullptr) {
        return serviceUnavailable();
    }
    QHttpServerResponse parseResponse(StatusCode::BadRequest);
    const auto object = requestObject(request, parseResponse);
    if (!object.has_value()) {
        return parseResponse;
    }

    manage::data::ReplaceBomItems command;
    command.id = id;
    qint64 revision = 0;
    QString itemError;
    if (!readInteger(*object, QStringLiteral("revision"), revision) ||
        revision < std::numeric_limits<int>::min() ||
        revision > std::numeric_limits<int>::max() ||
        !readItems(*object, true, command.items, itemError)) {
        return errorResponse(
            QStringLiteral("invalid_request"),
            itemError.isEmpty()
                ? QStringLiteral("integer revision and items are required")
                : itemError,
            StatusCode::BadRequest
        );
    }
    command.expectedRevision = static_cast<int>(revision);

    const auto result = service->replaceItems(std::move(command));
    return result.ok() ? QHttpServerResponse(templateJson(*result.value))
                       : serviceError(result.error, result.message);
}

} // namespace

void BomRoutes::registerRoutes(
    QHttpServer& server,
    manage::data::BomService* service,
    const std::shared_ptr<manage::auth::AuthService>& authService
) {
    server.route(
        QStringLiteral("/api/v1/boms"),
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
            return listBoms(service, request);
        }
    );
    server.route(
        QStringLiteral("/api/v1/boms"),
        QHttpServerRequest::Method::Post,
        [service, authService](const QHttpServerRequest& request) {
            if (auto failure = HttpAuthorization::require(
                    request,
                    authService,
                    {manage::auth::UserRole::Admin}
                )) {
                return std::move(*failure);
            }
            return createBom(service, request);
        }
    );
    server.route(
        QStringLiteral("/api/v1/boms/<arg>/items"),
        QHttpServerRequest::Method::Put,
        [service, authService](qint64 id, const QHttpServerRequest& request) {
            if (auto failure = HttpAuthorization::require(
                    request,
                    authService,
                    {manage::auth::UserRole::Admin}
                )) {
                return std::move(*failure);
            }
            return replaceBomItems(service, id, request);
        }
    );
    server.route(
        QStringLiteral("/api/v1/boms/<arg>/enabled"),
        QHttpServerRequest::Method::Patch,
        [service, authService](qint64 id, const QHttpServerRequest& request) {
            if (auto failure = HttpAuthorization::require(
                    request,
                    authService,
                    {manage::auth::UserRole::Admin}
                )) {
                return std::move(*failure);
            }
            return setBomEnabled(service, id, request);
        }
    );
    server.route(
        QStringLiteral("/api/v1/boms/<arg>"),
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
            return getBom(service, id);
        }
    );
    server.route(
        QStringLiteral("/api/v1/boms/<arg>"),
        QHttpServerRequest::Method::Put,
        [service, authService](qint64 id, const QHttpServerRequest& request) {
            if (auto failure = HttpAuthorization::require(
                    request,
                    authService,
                    {manage::auth::UserRole::Admin}
                )) {
                return std::move(*failure);
            }
            return updateBom(service, id, request);
        }
    );
}

} // namespace manage::server
