#pragma once

#include <memory>

class QHttpServer;

namespace manage::auth {
class AuthService;
}

namespace manage::data {
class MaterialBatchService;
}

namespace manage::server {

class MaterialBatchRoutes final {
public:
    static void registerRoutes(
        QHttpServer& server,
        manage::data::MaterialBatchService* service,
        const std::shared_ptr<manage::auth::AuthService>& authService
    );
};

} // namespace manage::server
