#pragma once

#include "manage/auth/auth_types.h"

#include <QHttpServerRequest>
#include <QHttpServerResponse>
#include <QString>

#include <initializer_list>
#include <memory>
#include <optional>

namespace manage::auth {
class AuthService;
}

namespace manage::server {

class HttpAuthorization final {
public:
    static QString bearerToken(const QHttpServerRequest& request);

    static QHttpServerResponse errorResponse(
        const manage::auth::AuthResult& result
    );

    static std::optional<QHttpServerResponse> require(
        const QHttpServerRequest& request,
        const std::shared_ptr<manage::auth::AuthService>& authService,
        std::initializer_list<manage::auth::UserRole> allowedRoles
    );
};

} // namespace manage::server
