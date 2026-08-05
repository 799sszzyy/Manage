#pragma once

#include <QHostAddress>
#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponse>
#include <QTcpServer>
#include <QtGlobal>

namespace manage::data {
class BomService;
}

namespace manage::server {

class ApiServer final {
public:
    explicit ApiServer(manage::data::BomService* bomService = nullptr);

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
};

} // namespace manage::server
