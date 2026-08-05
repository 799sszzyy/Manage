#pragma once

#include <memory>

class QHttpServer;

namespace manage::auth {
class AuthService;
class UserManagementService;
}

namespace manage::server {

class UserRoutes final {
public:
    static void registerRoutes(
        QHttpServer& server,
        const std::shared_ptr<manage::auth::UserManagementService>& service,
        const std::shared_ptr<manage::auth::AuthService>& authService
    );
};

} // namespace manage::server
