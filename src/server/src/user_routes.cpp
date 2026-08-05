#include "manage/server/user_routes.h"

#include "manage/auth/auth_service.h"
#include "manage/auth/user_management.h"
#include "manage/server/http_authorization.h"

#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponder>
#include <QHttpServerResponse>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <limits>
#include <optional>

namespace manage::server {
namespace {

using StatusCode = QHttpServerResponder::StatusCode;

QHttpServerResponse errorResponse(
    const QString& code,
    const QString& message,
    StatusCode status
) {
    return QHttpServerResponse(
        QJsonObject{{QStringLiteral("error"), code},
                    {QStringLiteral("message"), message}},
        status
    );
}

QHttpServerResponse serviceError(const manage::auth::UserManagementResult& result) {
    using Error = manage::auth::UserManagementError;
    switch (result.error) {
    case Error::Validation:
        return errorResponse(QStringLiteral("invalid_request"), result.message,
                             StatusCode::BadRequest);
    case Error::NotFound:
        return errorResponse(QStringLiteral("not_found"), result.message,
                             StatusCode::NotFound);
    case Error::Conflict:
        return errorResponse(QStringLiteral("revision_conflict"), result.message,
                             StatusCode::Conflict);
    case Error::ProtectedAccount:
        return errorResponse(QStringLiteral("protected_account"), result.message,
                             StatusCode::Conflict);
    case Error::RepositoryFailure:
        return errorResponse(QStringLiteral("database_error"), result.message,
                             StatusCode::InternalServerError);
    case Error::None:
        break;
    }
    return errorResponse(QStringLiteral("server_error"),
                         QStringLiteral("unable to manage user"),
                         StatusCode::InternalServerError);
}

QJsonObject userJson(const manage::auth::ManagedUser& user) {
    return {
        {QStringLiteral("id"), static_cast<qint64>(user.id)},
        {QStringLiteral("username"), user.username},
        {QStringLiteral("displayName"), user.displayName},
        {QStringLiteral("role"), manage::auth::roleCode(user.role)},
        {QStringLiteral("enabled"), user.enabled},
        {QStringLiteral("mustChangePassword"), user.mustChangePassword},
        {QStringLiteral("revision"), user.revision},
        {QStringLiteral("createdAt"), user.createdAt.toUTC().toString(Qt::ISODateWithMs)},
        {QStringLiteral("updatedAt"), user.updatedAt.toUTC().toString(Qt::ISODateWithMs)},
    };
}

QHttpServerResponse userResponse(
    const manage::auth::UserManagementResult& result,
    StatusCode status = StatusCode::Ok
) {
    return result.ok()
               ? QHttpServerResponse(QJsonObject{{QStringLiteral("user"), userJson(result.user)}}, status)
               : serviceError(result);
}

std::optional<QJsonObject> bodyObject(
    const QHttpServerRequest& request,
    QHttpServerResponse* failure
) {
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(request.body(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        *failure = errorResponse(QStringLiteral("invalid_json"),
                                 QStringLiteral("request body must be a JSON object"),
                                 StatusCode::BadRequest);
        return std::nullopt;
    }
    return document.object();
}

std::optional<manage::auth::UserRole> roleFromJson(const QJsonValue& value) {
    if (!value.isString()) {
        return std::nullopt;
    }
    const auto code = value.toString();
    if (code == QStringLiteral("admin")) return manage::auth::UserRole::Admin;
    if (code == QStringLiteral("quoter")) return manage::auth::UserRole::Quoter;
    if (code == QStringLiteral("viewer")) return manage::auth::UserRole::Viewer;
    return std::nullopt;
}

struct Actor final {
    quint64 id{};
    std::optional<QHttpServerResponse> failure;
};

Actor authorizeAdmin(
    const QHttpServerRequest& request,
    const std::shared_ptr<manage::auth::AuthService>& authService
) {
    if (!authService) {
        return {0, errorResponse(QStringLiteral("auth_unavailable"),
                                 QStringLiteral("authentication service is unavailable"),
                                 StatusCode::ServiceUnavailable)};
    }
    const auto result = authService->authorize(
        HttpAuthorization::bearerToken(request),
        {manage::auth::UserRole::Admin}
    );
    if (!result.succeeded()) {
        return {0, HttpAuthorization::errorResponse(result)};
    }
    return {result.session.user.id, std::nullopt};
}

QHttpServerResponse unavailable() {
    return errorResponse(QStringLiteral("user_management_unavailable"),
                         QStringLiteral("user management is unavailable"),
                         StatusCode::ServiceUnavailable);
}

} // namespace

void UserRoutes::registerRoutes(
    QHttpServer& server,
    const std::shared_ptr<manage::auth::UserManagementService>& service,
    const std::shared_ptr<manage::auth::AuthService>& authService
) {
    server.route(QStringLiteral("/api/v1/users"), QHttpServerRequest::Method::Get,
        [service, authService](const QHttpServerRequest& request) {
            auto actor = authorizeAdmin(request, authService);
            if (actor.failure) return std::move(*actor.failure);
            if (!service) return unavailable();
            manage::auth::UserSearch query;
            bool pageOk = true;
            bool pageSizeOk = true;
            const auto urlQuery = request.query();
            query.search = urlQuery.queryItemValue(QStringLiteral("search"));
            if (urlQuery.hasQueryItem(QStringLiteral("page")))
                query.page = urlQuery.queryItemValue(QStringLiteral("page")).toInt(&pageOk);
            if (urlQuery.hasQueryItem(QStringLiteral("pageSize")))
                query.pageSize = urlQuery.queryItemValue(QStringLiteral("pageSize")).toInt(&pageSizeOk);
            if (!pageOk || !pageSizeOk) {
                return errorResponse(QStringLiteral("invalid_request"),
                                     QStringLiteral("page and pageSize must be integers"),
                                     StatusCode::BadRequest);
            }
            const auto result = service->listUsers(query);
            if (!result.ok()) return serviceError(result);
            QJsonArray items;
            for (const auto& user : result.page.items) items.append(userJson(user));
            return QHttpServerResponse(QJsonObject{
                {QStringLiteral("items"), items},
                {QStringLiteral("total"), result.page.total},
                {QStringLiteral("page"), result.page.page},
                {QStringLiteral("pageSize"), result.page.pageSize},
            });
        });

    server.route(QStringLiteral("/api/v1/users"), QHttpServerRequest::Method::Post,
        [service, authService](const QHttpServerRequest& request) {
            auto actor = authorizeAdmin(request, authService);
            if (actor.failure) return std::move(*actor.failure);
            if (!service) return unavailable();
            QHttpServerResponse failure(StatusCode::BadRequest);
            const auto body = bodyObject(request, &failure);
            if (!body) return failure;
            const auto role = roleFromJson(body->value(QStringLiteral("role")));
            if (!body->value(QStringLiteral("username")).isString() ||
                !body->value(QStringLiteral("displayName")).isString() ||
                !body->value(QStringLiteral("temporaryPassword")).isString() || !role) {
                return errorResponse(QStringLiteral("invalid_request"),
                                     QStringLiteral("username, displayName, role and temporaryPassword are required"),
                                     StatusCode::BadRequest);
            }
            return userResponse(service->createUser({
                body->value(QStringLiteral("username")).toString(),
                body->value(QStringLiteral("displayName")).toString(),
                *role,
                body->value(QStringLiteral("temporaryPassword")).toString(),
            }), StatusCode::Created);
        });

    server.route(QStringLiteral("/api/v1/users/<arg>/enabled"), QHttpServerRequest::Method::Patch,
        [service, authService](qint64 id, const QHttpServerRequest& request) {
            auto actor = authorizeAdmin(request, authService);
            if (actor.failure) return std::move(*actor.failure);
            if (!service) return unavailable();
            QHttpServerResponse failure(StatusCode::BadRequest);
            const auto body = bodyObject(request, &failure);
            if (!body) return failure;
            if (id <= 0 || !body->value(QStringLiteral("enabled")).isBool() ||
                !body->value(QStringLiteral("revision")).isDouble()) {
                return errorResponse(QStringLiteral("invalid_request"),
                                     QStringLiteral("enabled and revision are required"),
                                     StatusCode::BadRequest);
            }
            return userResponse(service->setUserEnabled({
                static_cast<quint64>(id),
                body->value(QStringLiteral("enabled")).toBool(),
                body->value(QStringLiteral("revision")).toInt(),
                actor.id,
            }));
        });

    server.route(QStringLiteral("/api/v1/users/<arg>/reset-password"), QHttpServerRequest::Method::Post,
        [service, authService](qint64 id, const QHttpServerRequest& request) {
            auto actor = authorizeAdmin(request, authService);
            if (actor.failure) return std::move(*actor.failure);
            if (!service) return unavailable();
            QHttpServerResponse failure(StatusCode::BadRequest);
            const auto body = bodyObject(request, &failure);
            if (!body) return failure;
            if (id <= 0 || !body->value(QStringLiteral("revision")).isDouble() ||
                !body->value(QStringLiteral("temporaryPassword")).isString()) {
                return errorResponse(QStringLiteral("invalid_request"),
                                     QStringLiteral("revision and temporaryPassword are required"),
                                     StatusCode::BadRequest);
            }
            return userResponse(service->resetUserPassword({
                static_cast<quint64>(id),
                body->value(QStringLiteral("revision")).toInt(),
                body->value(QStringLiteral("temporaryPassword")).toString(),
            }));
        });

    server.route(QStringLiteral("/api/v1/users/<arg>"), QHttpServerRequest::Method::Put,
        [service, authService](qint64 id, const QHttpServerRequest& request) {
            auto actor = authorizeAdmin(request, authService);
            if (actor.failure) return std::move(*actor.failure);
            if (!service) return unavailable();
            QHttpServerResponse failure(StatusCode::BadRequest);
            const auto body = bodyObject(request, &failure);
            if (!body) return failure;
            const auto role = roleFromJson(body->value(QStringLiteral("role")));
            if (id <= 0 || !body->value(QStringLiteral("displayName")).isString() ||
                !body->value(QStringLiteral("revision")).isDouble() || !role) {
                return errorResponse(QStringLiteral("invalid_request"),
                                     QStringLiteral("displayName, role and revision are required; username cannot be changed"),
                                     StatusCode::BadRequest);
            }
            return userResponse(service->updateUser({
                static_cast<quint64>(id),
                body->value(QStringLiteral("displayName")).toString(),
                *role,
                body->value(QStringLiteral("revision")).toInt(),
                actor.id,
            }));
        });
}

} // namespace manage::server
