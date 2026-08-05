#pragma once

#include <memory>

class QHttpServer;

namespace manage::auth { class AuthService; }
namespace manage::data { class StatisticsRepository; }

namespace manage::server {

class StatisticsRoutes final {
public:
    static void registerRoutes(
        QHttpServer& server,
        manage::data::StatisticsRepository* repository,
        const std::shared_ptr<manage::auth::AuthService>& authService = nullptr
    );
};

} // namespace manage::server
