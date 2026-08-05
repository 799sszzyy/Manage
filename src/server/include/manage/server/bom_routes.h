#pragma once

#include <memory>

class QHttpServer;

namespace manage::auth {
class AuthService;
}

namespace manage::data {
class BomService;
}

namespace manage::server {

class BomRoutes final {
public:
    static void registerRoutes(
        QHttpServer& server,
        manage::data::BomService* service,
        const std::shared_ptr<manage::auth::AuthService>& authService = nullptr
    );
};

} // namespace manage::server
