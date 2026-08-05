#pragma once

#include <memory>

class QHttpServer;

namespace manage::auth {
class AuthService;
}

namespace manage::data {
class QuoteLifecycle;
}

namespace manage::server {

class QuoteRoutes final {
public:
    static void registerRoutes(
        QHttpServer& server,
        manage::data::QuoteLifecycle* lifecycle,
        const std::shared_ptr<manage::auth::AuthService>& authService = nullptr
    );
};

} // namespace manage::server
