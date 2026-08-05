#pragma once

class QHttpServer;

namespace manage::data {
class BomService;
}

namespace manage::server {

class BomRoutes final {
public:
    static void registerRoutes(
        QHttpServer& server,
        manage::data::BomService* service
    );
};

} // namespace manage::server
