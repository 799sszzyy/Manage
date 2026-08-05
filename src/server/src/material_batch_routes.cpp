#include "manage/server/material_batch_routes.h"

#include "manage/server/http_authorization.h"

#include "manage/data/material_batch_service.h"

#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponder>
#include <QHttpServerResponse>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <cmath>
#include <limits>
#include <utility>

namespace manage::server {
namespace {

using StatusCode = QHttpServerResponder::StatusCode;

QHttpServerResponse errorResponse(
    const QString& code,
    const QString& message,
    StatusCode status = StatusCode::BadRequest
) {
    return QHttpServerResponse(
        QJsonObject{{QStringLiteral("error"), code}, {QStringLiteral("message"), message}},
        status
    );
}

bool safeInteger(const QJsonValue& value, qint64& output) {
    if (!value.isDouble()) {
        return false;
    }
    const auto number = value.toDouble();
    constexpr auto maximum = static_cast<double>(9'007'199'254'740'991LL);
    if (!std::isfinite(number) || std::trunc(number) != number ||
        number < -maximum || number > maximum) {
        return false;
    }
    output = static_cast<qint64>(number);
    return true;
}

QJsonObject resultJson(const manage::data::MaterialBatchResult& result) {
    QJsonArray errors;
    for (const auto& item : result.errors) {
        errors.append(QJsonObject{
            {QStringLiteral("row"), item.sourceRow},
            {QStringLiteral("field"), item.field},
            {QStringLiteral("message"), item.message},
        });
    }
    return {
        {QStringLiteral("totalRows"), result.totalRows},
        {QStringLiteral("createCount"), result.createCount},
        {QStringLiteral("updateCount"), result.updateCount},
        {QStringLiteral("committed"), result.committed},
        {QStringLiteral("errors"), errors},
    };
}

QHttpServerResponse importMaterials(
    manage::data::MaterialBatchService* service,
    const QHttpServerRequest& request
) {
    if (service == nullptr) {
        return errorResponse(
            QStringLiteral("batch_import_unavailable"),
            QStringLiteral("物料批量导入服务不可用"),
            StatusCode::ServiceUnavailable
        );
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(request.body(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return errorResponse(QStringLiteral("invalid_json"), QStringLiteral("请求正文必须是 JSON 对象"));
    }
    const auto object = document.object();
    if (!object.value(QStringLiteral("rows")).isArray() ||
        (object.contains(QStringLiteral("validateOnly")) &&
         !object.value(QStringLiteral("validateOnly")).isBool())) {
        return errorResponse(QStringLiteral("invalid_request"),
                             QStringLiteral("rows 必须是数组，validateOnly 必须是布尔值"));
    }
    const auto inputRows = object.value(QStringLiteral("rows")).toArray();
    if (inputRows.size() > manage::data::MaterialBatchService::maximumRows) {
        return errorResponse(QStringLiteral("too_many_rows"), QStringLiteral("一次最多导入 5000 行"));
    }

    std::vector<manage::data::MaterialBatchRow> rows;
    rows.reserve(static_cast<std::size_t>(inputRows.size()));
    for (qsizetype index = 0; index < inputRows.size(); ++index) {
        if (!inputRows.at(index).isObject()) {
            return errorResponse(QStringLiteral("invalid_request"),
                                 QStringLiteral("第 %1 项必须是对象").arg(index + 1));
        }
        const auto item = inputRows.at(index).toObject();
        const auto sourceRowValue = item.value(QStringLiteral("sourceRow"));
        qint64 sourceRow = static_cast<qint64>(index) + 2;
        qint64 price = 0;
        if ((item.contains(QStringLiteral("sourceRow")) &&
             (!safeInteger(sourceRowValue, sourceRow) || sourceRow < 2 ||
              sourceRow > std::numeric_limits<int>::max())) ||
            !safeInteger(item.value(QStringLiteral("currentUnitPriceCents")), price) ||
            !item.value(QStringLiteral("code")).isString() ||
            !item.value(QStringLiteral("name")).isString() ||
            !item.value(QStringLiteral("unit")).isString() ||
            !item.value(QStringLiteral("isEnabled")).isBool()) {
            return errorResponse(QStringLiteral("invalid_request"),
                                 QStringLiteral("第 %1 项字段类型不正确").arg(index + 1));
        }
        manage::data::MaterialDraft draft;
        draft.code = item.value(QStringLiteral("code")).toString();
        draft.name = item.value(QStringLiteral("name")).toString();
        draft.specification = item.value(QStringLiteral("specification")).toString();
        draft.unit = item.value(QStringLiteral("unit")).toString();
        draft.category = item.value(QStringLiteral("category")).toString();
        draft.currentUnitPriceCents = price;
        draft.isEnabled = item.value(QStringLiteral("isEnabled")).toBool();
        rows.push_back({static_cast<int>(sourceRow), std::move(draft)});
    }

    const auto validateOnly = object.value(QStringLiteral("validateOnly")).toBool(true);
    const auto result = service->importMaterials(std::move(rows), validateOnly);
    const auto status = result.ok() ? StatusCode::Ok : StatusCode::BadRequest;
    return QHttpServerResponse(resultJson(result), status);
}

} // namespace

void MaterialBatchRoutes::registerRoutes(
    QHttpServer& server,
    manage::data::MaterialBatchService* service,
    const std::shared_ptr<manage::auth::AuthService>& authService
) {
    server.route(
        QStringLiteral("/api/v1/materials/import"),
        QHttpServerRequest::Method::Post,
        [service, authService](const QHttpServerRequest& request) {
            if (auto failure = HttpAuthorization::require(
                    request,
                    authService,
                    {manage::auth::UserRole::Admin}
                )) {
                return std::move(*failure);
            }
            return importMaterials(service, request);
        }
    );
}

} // namespace manage::server
