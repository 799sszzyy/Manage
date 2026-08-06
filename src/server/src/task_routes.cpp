#include "manage/server/task_routes.h"

#include "manage/server/http_authorization.h"

#include "manage/auth/auth_service.h"
#include "manage/data/task_service.h"

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

QHttpServerResponse serviceError(const manage::data::TaskError& error) {
    using ErrorCode = manage::data::TaskErrorCode;
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
    case ErrorCode::InvalidTransition:
        return errorResponse(
            QStringLiteral("invalid_transition"), error.message, StatusCode::Conflict
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
        QStringLiteral("server_error"), error.message, StatusCode::InternalServerError
    );
}

QJsonValue optionalInt64Json(const std::optional<std::int64_t>& value) {
    return value.has_value()
        ? QJsonValue(static_cast<qint64>(*value))
        : QJsonValue::Null;
}

QJsonValue optionalDateTimeJson(const std::optional<QDateTime>& value) {
    return value.has_value()
        ? QJsonValue(value->toUTC().toString(Qt::ISODateWithMs))
        : QJsonValue::Null;
}

QJsonObject taskJson(const manage::data::Task& task) {
    return {
        {QStringLiteral("id"), static_cast<qint64>(task.id)},
        {QStringLiteral("taskNumber"), task.taskNumber},
        {QStringLiteral("customerId"), optionalInt64Json(task.customerId)},
        {QStringLiteral("dispatchedBy"), static_cast<qint64>(task.dispatchedBy)},
        {QStringLiteral("assignedEngineerId"),
         static_cast<qint64>(task.assignedEngineerId)},
        {QStringLiteral("expectedCompletionAt"),
         optionalDateTimeJson(task.expectedCompletionAt)},
        {QStringLiteral("status"), manage::data::taskStatusCode(task.status)},
        {QStringLiteral("title"), task.title},
        {QStringLiteral("notes"), task.notes},
        {QStringLiteral("quoteId"), optionalInt64Json(task.quoteId)},
        {QStringLiteral("revision"), static_cast<qint64>(task.revision)},
        {QStringLiteral("createdAt"),
         task.createdAt.toUTC().toString(Qt::ISODateWithMs)},
        {QStringLiteral("updatedAt"),
         task.updatedAt.toUTC().toString(Qt::ISODateWithMs)},
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
    if (value.isNull()) {
        if (required) {
            return false;
        }
        result = defaultValue;
        return true;
    }
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

bool readOptionalInt64(
    const QJsonObject& object,
    const QString& key,
    std::optional<std::int64_t>& result
) {
    if (!object.contains(key) || object.value(key).isNull()) {
        result.reset();
        return true;
    }
    qint64 raw = 0;
    if (!readInteger(object, key, raw, true)) {
        return false;
    }
    result = raw;
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
    if (object.value(key).isNull()) {
        result.clear();
        return true;
    }
    if (!object.value(key).isString()) {
        return false;
    }
    result = object.value(key).toString();
    return true;
}

bool readOptionalDateTime(
    const QJsonObject& object,
    const QString& key,
    std::optional<QDateTime>& result
) {
    if (!object.contains(key) || object.value(key).isNull()) {
        result.reset();
        return true;
    }
    if (!object.value(key).isString()) {
        return false;
    }
    const auto text = object.value(key).toString();
    auto parsed = QDateTime::fromString(text, Qt::ISODateWithMs);
    if (!parsed.isValid()) {
        parsed = QDateTime::fromString(text, Qt::ISODate);
    }
    if (!parsed.isValid()) {
        return false;
    }
    result = parsed;
    return true;
}

std::optional<manage::data::TaskDraft> taskDraft(
    const QJsonObject& object,
    QHttpServerResponse* failure
) {
    manage::data::TaskDraft draft;
    qint64 dispatchedBy = 0;
    qint64 assignedEngineerId = 0;
    if (!readOptionalInt64(object, QStringLiteral("customerId"), draft.customerId) ||
        !readInteger(object, QStringLiteral("dispatchedBy"), dispatchedBy, true) ||
        !readInteger(
            object, QStringLiteral("assignedEngineerId"), assignedEngineerId, true
        ) ||
        !readOptionalDateTime(
            object, QStringLiteral("expectedCompletionAt"),
            draft.expectedCompletionAt
        ) ||
        !readString(object, QStringLiteral("title"), draft.title, false) ||
        !readString(object, QStringLiteral("notes"), draft.notes, false)) {
        *failure = errorResponse(
            QStringLiteral("invalid_request"),
            QStringLiteral(
                "dispatchedBy and assignedEngineerId must be safe integers; "
                "customerId/expectedCompletionAt are optional"
            )
        );
        return std::nullopt;
    }
    draft.dispatchedBy = dispatchedBy;
    draft.assignedEngineerId = assignedEngineerId;
    return draft;
}

QHttpServerResponse taskPageResponse(
    const manage::data::TaskResult<manage::data::TaskPage>& result
) {
    if (!result.ok()) {
        return serviceError(*result.error);
    }
    QJsonArray items;
    for (const auto& task : result.value->items) {
        items.append(taskJson(task));
    }
    const auto& page = *result.value;
    const auto totalPages = page.total == 0
        ? 0
        : (page.total + page.pageSize - 1) / page.pageSize;
    return QHttpServerResponse(QJsonObject{
        {QStringLiteral("items"), items},
        {QStringLiteral("page"), page.page},
        {QStringLiteral("pageSize"), page.pageSize},
        {QStringLiteral("total"), static_cast<qint64>(page.total)},
        {QStringLiteral("totalPages"), totalPages},
    });
}

QHttpServerResponse taskResponse(
    const manage::data::TaskResult<manage::data::Task>& result,
    StatusCode successStatus = StatusCode::Ok
) {
    if (!result.ok()) {
        return serviceError(*result.error);
    }
    return QHttpServerResponse(
        QJsonObject{{QStringLiteral("task"), taskJson(*result.value)}},
        successStatus
    );
}

} // namespace

void TaskRoutes::registerRoutes(
    QHttpServer& server,
    manage::data::TaskService* service,
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
        QStringLiteral("/api/v1/tasks"),
        QHttpServerRequest::Method::Get,
        [service, authService, readRoles](const QHttpServerRequest& request) {
            if (auto failure = HttpAuthorization::require(request, authService, readRoles)) {
                return std::move(*failure);
            }
            if (service == nullptr) {
                return errorResponse(
                    QStringLiteral("database_unavailable"),
                    QStringLiteral("task storage is unavailable"),
                    StatusCode::ServiceUnavailable
                );
            }
            QUrlQuery query(request.url());
            manage::data::TaskSearchQuery pageQuery;
            pageQuery.page = query.queryItemValue(QStringLiteral("page")).toInt();
            pageQuery.pageSize = query.queryItemValue(QStringLiteral("pageSize")).toInt();
            if (pageQuery.page <= 0) pageQuery.page = 1;
            if (pageQuery.pageSize <= 0) pageQuery.pageSize = 20;
            pageQuery.search = query.queryItemValue(QStringLiteral("search"));
            if (query.hasQueryItem(QStringLiteral("status"))) {
                pageQuery.status = manage::data::taskStatusFromCode(
                    query.queryItemValue(QStringLiteral("status"))
                );
            }
            if (query.hasQueryItem(QStringLiteral("engineer"))) {
                qint64 engineer = query
                    .queryItemValue(QStringLiteral("engineer"))
                    .toLongLong();
                if (engineer > 0) pageQuery.assignedEngineerId = engineer;
            }
            if (query.hasQueryItem(QStringLiteral("dispatcher"))) {
                qint64 dispatcher = query
                    .queryItemValue(QStringLiteral("dispatcher"))
                    .toLongLong();
                if (dispatcher > 0) pageQuery.dispatchedBy = dispatcher;
            }
            return taskPageResponse(service->list(pageQuery));
        }
    );

    server.route(
        QStringLiteral("/api/v1/tasks"),
        QHttpServerRequest::Method::Post,
        [service, authService, writeRoles](const QHttpServerRequest& request) {
            if (auto failure = HttpAuthorization::require(request, authService, writeRoles)) {
                return std::move(*failure);
            }
            if (service == nullptr) {
                return errorResponse(
                    QStringLiteral("database_unavailable"),
                    QStringLiteral("task storage is unavailable"),
                    StatusCode::ServiceUnavailable
                );
            }
            QHttpServerResponse failureResponse(StatusCode::BadRequest);
            const auto parsed = requestObject(request, failureResponse);
            if (!parsed.has_value()) {
                return failureResponse;
            }
            QHttpServerResponse draftFailure(StatusCode::BadRequest);
            const auto draft = taskDraft(*parsed, &draftFailure);
            if (!draft.has_value()) {
                return draftFailure;
            }
            return taskResponse(service->create(*draft), StatusCode::Created);
        }
    );

    server.route(
        QStringLiteral("/api/v1/tasks/<arg>"),
        QHttpServerRequest::Method::Get,
        [service, authService, readRoles](qint64 id, const QHttpServerRequest& request) {
            if (auto failure = HttpAuthorization::require(request, authService, readRoles)) {
                return std::move(*failure);
            }
            if (service == nullptr) {
                return errorResponse(
                    QStringLiteral("database_unavailable"),
                    QStringLiteral("task storage is unavailable"),
                    StatusCode::ServiceUnavailable
                );
            }
            return taskResponse(service->get(id));
        }
    );

    server.route(
        QStringLiteral("/api/v1/tasks/<arg>"),
        QHttpServerRequest::Method::Put,
        [service, authService, writeRoles](qint64 id, const QHttpServerRequest& request) {
            if (auto failure = HttpAuthorization::require(request, authService, writeRoles)) {
                return std::move(*failure);
            }
            if (service == nullptr) {
                return errorResponse(
                    QStringLiteral("database_unavailable"),
                    QStringLiteral("task storage is unavailable"),
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
            const auto draft = taskDraft(object, &draftFailure);
            if (!draft.has_value()) {
                return draftFailure;
            }
            return taskResponse(
                service->update(id, static_cast<std::uint32_t>(revision), *draft)
            );
        }
    );

    server.route(
        QStringLiteral("/api/v1/tasks/<arg>/status"),
        QHttpServerRequest::Method::Patch,
        [service, authService, writeRoles](qint64 id, const QHttpServerRequest& request) {
            if (auto failure = HttpAuthorization::require(request, authService, writeRoles)) {
                return std::move(*failure);
            }
            if (service == nullptr) {
                return errorResponse(
                    QStringLiteral("database_unavailable"),
                    QStringLiteral("task storage is unavailable"),
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
            QString statusText;
            if (!readInteger(object, QStringLiteral("revision"), revision, true) ||
                !readString(object, QStringLiteral("status"), statusText, true)) {
                return errorResponse(
                    QStringLiteral("invalid_request"),
                    QStringLiteral("revision and status are required")
                );
            }
            const auto target = manage::data::taskStatusFromCode(statusText);
            if (!target.has_value()) {
                return errorResponse(
                    QStringLiteral("invalid_request"),
                    QStringLiteral("status must be dispatched/in_progress/completed/cancelled")
                );
            }
            return taskResponse(
                service->setStatus(id, static_cast<std::uint32_t>(revision), *target)
            );
        }
    );

    server.route(
        QStringLiteral("/api/v1/tasks/<arg>/quote"),
        QHttpServerRequest::Method::Patch,
        [service, authService, writeRoles](qint64 id, const QHttpServerRequest& request) {
            if (auto failure = HttpAuthorization::require(request, authService, writeRoles)) {
                return std::move(*failure);
            }
            if (service == nullptr) {
                return errorResponse(
                    QStringLiteral("database_unavailable"),
                    QStringLiteral("task storage is unavailable"),
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
            std::optional<std::int64_t> quoteId;
            if (!readInteger(object, QStringLiteral("revision"), revision, true) ||
                !readOptionalInt64(object, QStringLiteral("quoteId"), quoteId)) {
                return errorResponse(
                    QStringLiteral("invalid_request"),
                    QStringLiteral("revision is required and quoteId must be a safe integer or null")
                );
            }
            return taskResponse(
                service->setQuoteId(id, static_cast<std::uint32_t>(revision), quoteId)
            );
        }
    );
}

} // namespace manage::server
