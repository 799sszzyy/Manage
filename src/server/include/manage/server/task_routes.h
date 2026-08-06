#pragma once

#include <memory>

class QHttpServer;

namespace manage::auth {
class AuthService;
}

namespace manage::data {
class TaskService;
}

namespace manage::server {

class TaskRoutes final {
public:
    static void registerRoutes(
        QHttpServer& server,
        manage::data::TaskService* service,
        const std::shared_ptr<manage::auth::AuthService>& authService = nullptr
    );
};

} // namespace manage::server
