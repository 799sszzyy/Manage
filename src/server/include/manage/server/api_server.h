#pragma once

#include <QHostAddress>
#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponse>
#include <QTcpServer>
#include <QtGlobal>

#include <memory>

namespace manage::auth {
class AuthService;
}

namespace manage::server {

class ApiServer final {
public:
    ApiServer();
    explicit ApiServer(std::shared_ptr<manage::auth::AuthService> authService);

    quint16 listen(
        const QHostAddress& address = QHostAddress::LocalHost,
        quint16 port = 18'080
    );

private:
    QHttpServerResponse healthResponse() const;
    QHttpServerResponse calculateQuoteResponse(
        const QHttpServerRequest& request
    ) const;
    QHttpServerResponse bootstrapResponse(
        const QHttpServerRequest& request
    ) const;
    QHttpServerResponse loginResponse(const QHttpServerRequest& request) const;
    QHttpServerResponse logoutResponse(const QHttpServerRequest& request) const;
    QHttpServerResponse meResponse(const QHttpServerRequest& request) const;
    QHttpServerResponse changePasswordResponse(
        const QHttpServerRequest& request
    ) const;

    QHttpServer server_;
    QTcpServer* tcpServer_{};
    std::shared_ptr<manage::auth::AuthService> authService_;
};

} // namespace manage::server
