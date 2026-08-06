#pragma once

#include <memory>

class QHttpServer;

namespace manage::auth {
class AuthService;
}

namespace manage::data {
class ProcessStepService;
}

namespace manage::server {

class ProcessStepRoutes final {
public:
    static void registerRoutes(
        QHttpServer& server,
        manage::data::ProcessStepService* service,
        const std::shared_ptr<manage::auth::AuthService>& authService = nullptr
    );
};

} // namespace manage::server
