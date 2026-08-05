#pragma once

#include <QHostAddress>
#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponse>
#include <QTcpServer>
#include <QtGlobal>

#include <memory>

namespace manage::data {
class CatalogRepository;
class CatalogService;
}

namespace manage::server {

class ApiServer final {
public:
    ApiServer();
    explicit ApiServer(
        std::shared_ptr<manage::data::CatalogRepository> catalogRepository
    );

    quint16 listen(
        const QHostAddress& address = QHostAddress::LocalHost,
        quint16 port = 18'080
    );

private:
    QHttpServerResponse healthResponse() const;
    QHttpServerResponse calculateQuoteResponse(
        const QHttpServerRequest& request
    ) const;

    QHttpServer server_;
    QTcpServer* tcpServer_{};
    std::shared_ptr<manage::data::CatalogService> catalogService_;
};

} // namespace manage::server
