#pragma once

#include <memory>

class QHttpServer;

namespace manage::data {
class CatalogService;
}

namespace manage::server {

void registerCatalogRoutes(
    QHttpServer& server,
    const std::shared_ptr<manage::data::CatalogService>& service
);

} // namespace manage::server
