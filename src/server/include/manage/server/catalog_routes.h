#pragma once

#include <memory>

class QHttpServer;

namespace manage::auth {
class AuthService;
}

namespace manage::data {
class CatalogService;
}

namespace manage::server {

void registerCatalogRoutes(
    QHttpServer& server,
    const std::shared_ptr<manage::data::CatalogService>& service,
    const std::shared_ptr<manage::auth::AuthService>& authService = nullptr
);

} // namespace manage::server
