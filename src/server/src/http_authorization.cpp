#include "manage/server/http_authorization.h"

#include "manage/auth/auth_service.h"

#include <QHttpServerResponder>
#include <QJsonObject>

namespace manage::server {
namespace {

using StatusCode = QHttpServerResponder::StatusCode;

StatusCode statusCode(manage::auth::AuthErrorCode code) {
    using ErrorCode = manage::auth::AuthErrorCode;
    switch (code) {
    case ErrorCode::InvalidRequest:
        return StatusCode::BadRequest;
    case ErrorCode::BootstrapUnavailable:
        return StatusCode::Conflict;
    case ErrorCode::InvalidCredentials:
    case ErrorCode::Unauthorized:
    case ErrorCode::SessionExpired:
        return StatusCode::Unauthorized;
    case ErrorCode::AccountDisabled:
    case ErrorCode::PasswordChangeRequired:
    case ErrorCode::Forbidden:
        return StatusCode::Forbidden;
    case ErrorCode::RepositoryFailure:
        return StatusCode::InternalServerError;
    case ErrorCode::None:
        return StatusCode::Ok;
    }
    return StatusCode::InternalServerError;
}

QHttpServerResponse unavailableResponse() {
    return QHttpServerResponse(
        QJsonObject{
            {QStringLiteral("error"), QStringLiteral("auth_unavailable")},
            {
                QStringLiteral("message"),
                QStringLiteral("authentication service is unavailable")
            },
        },
        StatusCode::ServiceUnavailable
    );
}

} // namespace

QString HttpAuthorization::bearerToken(const QHttpServerRequest& request) {
    const auto authorization = request.value("Authorization").trimmed();
    const auto separator = authorization.indexOf(' ');
    if (separator <= 0 ||
        authorization.left(separator).compare("Bearer", Qt::CaseInsensitive) != 0) {
        return {};
    }

    const auto token = authorization.mid(separator + 1).trimmed();
    if (token.isEmpty()) {
        return {};
    }
    for (const auto character : token) {
        if (character == ' ' || character == '\t' || character == '\r' ||
            character == '\n') {
            return {};
        }
    }
    return QString::fromLatin1(token);
}

QHttpServerResponse HttpAuthorization::errorResponse(
    const manage::auth::AuthResult& result
) {
    return QHttpServerResponse(
        QJsonObject{
            {
                QStringLiteral("error"),
                manage::auth::authErrorCode(result.error)
            },
            {QStringLiteral("message"), result.message},
        },
        statusCode(result.error)
    );
}

std::optional<QHttpServerResponse> HttpAuthorization::require(
    const QHttpServerRequest& request,
    const std::shared_ptr<manage::auth::AuthService>& authService,
    std::initializer_list<manage::auth::UserRole> allowedRoles
) {
    if (!authService) {
        return unavailableResponse();
    }

    const auto result = authService->authorize(
        bearerToken(request),
        allowedRoles
    );
    if (!result.succeeded()) {
        return errorResponse(result);
    }
    return std::nullopt;
}

} // namespace manage::server
