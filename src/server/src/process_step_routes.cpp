#include "manage/server/process_step_routes.h"

#include "manage/server/http_authorization.h"

#include "manage/auth/auth_service.h"
#include "manage/data/process_step_service.h"

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
#include <limits>
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
    StatusCode status = StatusCode::BadRequest
) {
    return QHttpServerResponse(
        QJsonObject{
            {QStringLiteral("error"), code},
            {QStringLiteral("message"), message},
        },
        status
    );
}

QHttpServerResponse serviceError(
    const manage::data::ProcessStepError& error
) {
    using ErrorCode = manage::data::ProcessStepErrorCode;
    switch (error.code) {
    case ErrorCode::InvalidRequest:
        return errorResponse(QStringLiteral("invalid_request"), error.message);
    case ErrorCode::NotFound:
        return errorResponse(
            QStringLiteral("not_found"), error.message, StatusCode::NotFound
        );
    case ErrorCode::RevisionConflict:
        return errorResponse(
            QStringLiteral("revision_conflict"), error.message, StatusCode::Conflict
        );
    case ErrorCode::DuplicateCode:
        return errorResponse(
            QStringLiteral("duplicate_code"), error.message, StatusCode::Conflict
        );
    case ErrorCode::Database:
        return errorResponse(
            QStringLiteral("database_error"),
            error.message,
            StatusCode::InternalServerError
        );
    case ErrorCode::None:
        break;
    }
    return errorResponse(
        QStringLiteral("server_error"),
        error.message,
        StatusCode::InternalServerError
    );
}

QJsonObject processStepJson(const manage::data::ProcessStep& step) {
    return {
        {QStringLiteral("id"), step.id},
        {QStringLiteral("code"), step.code},
        {QStringLiteral("name"), step.name},
        {QStringLiteral("laborMinutes"), step.laborMinutes},
        {QStringLiteral("description"), step.description},
        {QStringLiteral("isEnabled"), step.isEnabled},
        {QStringLiteral("revision"), static_cast<qint64>(step.revision)},
        {QStringLiteral("createdAt"), step.createdAt.toUTC().toString(Qt::ISODateWithMs)},
        {QStringLiteral("updatedAt"), step.updatedAt.toUTC().toString(Qt::ISODateWithMs)},
    };
}

std::optional<QJsonObject> requestObject(
    const QHttpServerRequest& request,
    QHttpServerResponse& failure
) {
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(request.body(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        failure = errorResponse(
            QStringLiteral("invalid_json"),
            QStringLiteral("request body must be a JSON object")
        );
        return std::nullopt;
    }
    return document.object();
}

bool readInteger(
    const QJsonObject& object,
    const QString& key,
    qint64& result,
    bool required,
    qint64 defaultValue = 0
) {
    if (!object.contains(key)) {
        if (required) {
            return false;
        }
        result = defaultValue;
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
    result = static_cast<qint64>(number);
    return true;
}

bool readBool(
    const QJsonObject& object,
    const QString& key,
    bool& result,
    bool required,
    bool defaultValue = false
) {
    if (!object.contains(key)) {
        if (required) {
            return false;
        }
        result = defaultValue;
        return true;
    }
    if (!object.value(key).isBool()) {
        return false;
    }
    result = object.value(key).toBool();
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

std::optional<manage::data::ProcessStepDraft> stepDraft(
    const QJsonObject& object,
    QHttpServerResponse* failure
) {
    manage::data::ProcessStepDraft draft;
    qint64 laborMinutes = 0;
    if (!readString(object, QStringLiteral("code"), draft.code, true) ||
        !readString(object, QStringLiteral("name"), draft.name, true) ||
        !readString(object, QStringLiteral("description"), draft.description, false) ||
        !readInteger(object, QStringLiteral("laborMinutes"), laborMinutes, false, 0) ||
        laborMinutes > std::numeric_limits<int>::max() ||
        !readBool(object, QStringLiteral("isEnabled"), draft.isEnabled, false, true)) {
        *failure = errorResponse(
            QStringLiteral("invalid_request"),
            QStringLiteral(
                "code and name must be strings; laborMinutes must be a safe integer"
            )
        );
        return std::nullopt;
    }
    draft.laborMinutes = static_cast<int>(laborMinutes);
    return draft;
}

QHttpServerResponse stepPageResponse(
    const manage::data::ProcessStepResult<manage::data::ProcessStepPage>& result
) {
    if (!result.ok()) {
        return serviceError(*result.error);
    }
    QJsonArray items;
    for (const auto& step : result.value->items) {
        items.append(processStepJson(step));
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

QHttpServerResponse stepResponse(
    const manage::data::ProcessStepResult<manage::data::ProcessStep>& result,
    StatusCode successStatus = StatusCode::Ok
) {
    if (!result.ok()) {
        return serviceError(*result.error);
    }
    return QHttpServerResponse(
        QJsonObject{{QStringLiteral("processStep"), processStepJson(*result.value)}},
        successStatus
    );
}

} // namespace

void ProcessStepRoutes::registerRoutes(
    QHttpServer& server,
    manage::data::ProcessStepService* service,
    const std::shared_ptr<manage::auth::AuthService>& authService
) {
    const auto writeRoles = {
        manage::auth::UserRole::Admin,
        manage::auth::UserRole::Quoter,
    };
    const auto readRoles = {
        manage::auth::UserRole::Admin,
        manage::auth::UserRole::Quoter,
        manage::auth::UserRole::Viewer,
    };

    server.route(
        QStringLiteral("/api/v1/process-steps"),
        QHttpServerRequest::Method::Get,
        [service, authService](const QHttpServerRequest& request) {
            if (auto failure = HttpAuthorization::require(request, authService, readRoles)) {
                return std::move(*failure);
            }
            if (service == nullptr) {
                return errorResponse(
                    QStringLiteral("database_unavailable"),
                    QStringLiteral("process step storage is unavailable"),
                    StatusCode::ServiceUnavailable
                );
            }
            QUrlQuery query(request.url());
            manage::data::PageQuery pageQuery;
            pageQuery.page = query.queryItemValue(QStringLiteral("page")).toInt();
            pageQuery.pageSize =
                query.queryItemValue(QStringLiteral("pageSize")).toInt();
            if (pageQuery.page <= 0) pageQuery.page = 1;
            if (pageQuery.pageSize <= 0) pageQuery.pageSize = 20;
            pageQuery.search = query.queryItemValue(QStringLiteral("search"));
            if (query.hasQueryItem(QStringLiteral("enabled"))) {
                const auto enabled = query.queryItemValue(QStringLiteral("enabled"));
                if (enabled == QStringLiteral("true")) {
                    pageQuery.enabled = true;
                } else if (enabled == QStringLiteral("false")) {
                    pageQuery.enabled = false;
                }
            }
            return stepPageResponse(service->list(pageQuery));
        }
    );

    server.route(
        QStringLiteral("/api/v1/process-steps"),
        QHttpServerRequest::Method::Post,
        [service, authService](const QHttpServerRequest& request) {
            if (auto failure = HttpAuthorization::require(request, authService, writeRoles)) {
                return std::move(*failure);
            }
            if (service == nullptr) {
                return errorResponse(
                    QStringLiteral("database_unavailable"),
                    QStringLiteral("process step storage is unavailable"),
                    StatusCode::ServiceUnavailable
                );
            }
            QHttpServerResponse failureResponse(StatusCode::BadRequest);
            const auto parsed = requestObject(request, failureResponse);
            if (!parsed.has_value()) {
                return failureResponse;
            }
            const auto object = *parsed;
            QHttpServerResponse draftFailure(StatusCode::BadRequest);
            const auto draft = stepDraft(object, &draftFailure);
            if (!draft.has_value()) {
                return draftFailure;
            }
            return stepResponse(service->create(*draft), StatusCode::Created);
        }
    );

    server.route(
        QStringLiteral("/api/v1/process-steps/<arg>"),
        QHttpServerRequest::Method::Get,
        [service, authService](qint64 id, const QHttpServerRequest& request) {
            if (auto failure = HttpAuthorization::require(request, authService, readRoles)) {
                return std::move(*failure);
            }
            if (service == nullptr) {
                return errorResponse(
                    QStringLiteral("database_unavailable"),
                    QStringLiteral("process step storage is unavailable"),
                    StatusCode::ServiceUnavailable
                );
            }
            return stepResponse(service->get(id));
        }
    );

    server.route(
        QStringLiteral("/api/v1/process-steps/<arg>"),
        QHttpServerRequest::Method::Put,
        [service, authService](qint64 id, const QHttpServerRequest& request) {
            if (auto failure = HttpAuthorization::require(request, authService, writeRoles)) {
                return std::move(*failure);
            }
            if (service == nullptr) {
                return errorResponse(
                    QStringLiteral("database_unavailable"),
                    QStringLiteral("process step storage is unavailable"),
                    StatusCode::ServiceUnavailable
                );
            }
            QHttpServerResponse failureResponse(StatusCode::BadRequest);
            const auto parsed = requestObject(request, failureResponse);
            if (!parsed.has_value()) {
                return failureResponse;
            }
            const auto object = *parsed;
            qint64 revision = 0;
            if (!readInteger(object, QStringLiteral("revision"), revision, true)) {
                return errorResponse(
                    QStringLiteral("invalid_request"),
                    QStringLiteral("revision must be a safe integer")
                );
            }
            QHttpServerResponse draftFailure(StatusCode::BadRequest);
            const auto draft = stepDraft(object, &draftFailure);
            if (!draft.has_value()) {
                return draftFailure;
            }
            return stepResponse(
                service->update(id, static_cast<std::uint32_t>(revision), *draft)
            );
        }
    );

    server.route(
        QStringLiteral("/api/v1/process-steps/<arg>/enabled"),
        QHttpServerRequest::Method::Patch,
        [service, authService](qint64 id, const QHttpServerRequest& request) {
            if (auto failure = HttpAuthorization::require(request, authService, writeRoles)) {
                return std::move(*failure);
            }
            if (service == nullptr) {
                return errorResponse(
                    QStringLiteral("database_unavailable"),
                    QStringLiteral("process step storage is unavailable"),
                    StatusCode::ServiceUnavailable
                );
            }
            QHttpServerResponse failureResponse(StatusCode::BadRequest);
            const auto parsed = requestObject(request, failureResponse);
            if (!parsed.has_value()) {
                return failureResponse;
            }
            const auto object = *parsed;
            bool enabled = false;
            qint64 revision = 0;
            if (!readBool(object, QStringLiteral("enabled"), enabled, true) ||
                !readInteger(object, QStringLiteral("revision"), revision, true)) {
                return errorResponse(
                    QStringLiteral("invalid_request"),
                    QStringLiteral("enabled and revision are required")
                );
            }
            return stepResponse(
                service->setEnabled(id, static_cast<std::uint32_t>(revision), enabled)
            );
        }
    );
}

} // namespace manage::server
