#include "manage/desktop/api_client.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QHostAddress>
#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponder>
#include <QHttpServerResponse>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QTcpServer>
#include <QTimer>

#include <cstdlib>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using manage::desktop::ApiClient;
using manage::desktop::ApiErrorKind;
using manage::desktop::ApiResponse;
using StatusCode = QHttpServerResponder::StatusCode;

class TestFailure final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw TestFailure(message);
    }
}

QJsonObject requestObject(const QHttpServerRequest& request) {
    return QJsonDocument::fromJson(request.body()).object();
}

ApiResponse waitFor(
    const std::function<void(ApiClient::Callback)>& begin,
    int timeoutMilliseconds = 5'000
) {
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    std::optional<ApiResponse> result;

    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    begin([&](ApiResponse response) {
        result = std::move(response);
        loop.quit();
    });
    timeout.start(timeoutMilliseconds);
    if (!result.has_value()) {
        loop.exec();
    }
    require(result.has_value(), "API callback timed out");
    return std::move(*result);
}

struct TestApi final {
    QHttpServer server;
    QTcpServer tcpServer;
    quint16 port{};
    QByteArray lastAuthorization;
    QJsonObject lastBody;
    QString lastPath;

    TestApi() {
        server.route(
            QStringLiteral("/root/api/v1/auth/bootstrap"),
            QHttpServerRequest::Method::Post,
            [this](const QHttpServerRequest& request) {
                lastBody = requestObject(request);
                lastPath = request.url().path();
                return QHttpServerResponse(
                    QJsonObject{{
                        QStringLiteral("user"),
                        QJsonObject{{QStringLiteral("role"), QStringLiteral("admin")}}
                    }},
                    StatusCode::Created
                );
            }
        );
        server.route(
            QStringLiteral("/root/api/v1/auth/login"),
            QHttpServerRequest::Method::Post,
            [this](const QHttpServerRequest& request) {
                lastBody = requestObject(request);
                return QHttpServerResponse(QJsonObject{
                    {QStringLiteral("accessToken"), QStringLiteral("secret-session-token")},
                    {QStringLiteral("expiresAt"), QStringLiteral("2026-08-06T12:00:00Z")},
                    {
                        QStringLiteral("user"),
                        QJsonObject{
                            {QStringLiteral("username"), QStringLiteral("admin")},
                            {QStringLiteral("role"), QStringLiteral("admin")},
                            {QStringLiteral("mustChangePassword"), true},
                        }
                    },
                });
            }
        );
        server.route(
            QStringLiteral("/root/api/v1/auth/me"),
            QHttpServerRequest::Method::Get,
            [this](const QHttpServerRequest& request) {
                lastAuthorization = request.value("Authorization");
                return QHttpServerResponse(QJsonObject{
                    {
                        QStringLiteral("user"),
                        QJsonObject{
                            {QStringLiteral("username"), QStringLiteral("admin")},
                            {QStringLiteral("mustChangePassword"), false},
                        }
                    },
                    {QStringLiteral("expiresAt"), QStringLiteral("2026-08-06T13:00:00Z")},
                });
            }
        );
        server.route(
            QStringLiteral("/root/api/v1/auth/change-password"),
            QHttpServerRequest::Method::Post,
            [this](const QHttpServerRequest& request) {
                lastAuthorization = request.value("Authorization");
                lastBody = requestObject(request);
                return QHttpServerResponse(QJsonObject{
                    {QStringLiteral("status"), QStringLiteral("password_changed")},
                    {
                        QStringLiteral("user"),
                        QJsonObject{{QStringLiteral("mustChangePassword"), false}}
                    },
                });
            }
        );
        server.route(
            QStringLiteral("/root/api/v1/auth/logout"),
            QHttpServerRequest::Method::Post,
            [this](const QHttpServerRequest& request) {
                lastAuthorization = request.value("Authorization");
                return QHttpServerResponse(QJsonObject{
                    {QStringLiteral("status"), QStringLiteral("logged_out")},
                });
            }
        );
        server.route(
            QStringLiteral("/root/api/v1/protected"),
            QHttpServerRequest::Method::Get,
            [this](const QHttpServerRequest& request) {
                lastAuthorization = request.value("Authorization");
                return QHttpServerResponse(QJsonObject{
                    {QStringLiteral("ok"), true},
                    {QStringLiteral("query"), request.url().query()},
                });
            }
        );
        server.route(
            QStringLiteral("/root/api/v1/forbidden"),
            QHttpServerRequest::Method::Get,
            []() {
                return QHttpServerResponse(
                    QJsonObject{
                        {QStringLiteral("error"), QStringLiteral("forbidden")},
                        {QStringLiteral("message"), QStringLiteral("role is not allowed")},
                    },
                    StatusCode::Forbidden
                );
            }
        );
        server.route(
            QStringLiteral("/root/api/v1/expired"),
            QHttpServerRequest::Method::Get,
            []() {
                return QHttpServerResponse(
                    QJsonObject{
                        {QStringLiteral("error"), QStringLiteral("session_expired")},
                        {QStringLiteral("message"), QStringLiteral("session expired")},
                    },
                    StatusCode::Unauthorized
                );
            }
        );
        server.route(
            QStringLiteral("/root/api/v1/create"),
            QHttpServerRequest::Method::Post,
            [this](const QHttpServerRequest& request) {
                lastBody = requestObject(request);
                return QHttpServerResponse(lastBody, StatusCode::Created);
            }
        );
        server.route(
            QStringLiteral("/root/api/v1/update"),
            QHttpServerRequest::Method::Put,
            [this](const QHttpServerRequest& request) {
                lastBody = requestObject(request);
                return QHttpServerResponse(lastBody);
            }
        );
        server.route(
            QStringLiteral("/root/api/v1/enabled"),
            QHttpServerRequest::Method::Patch,
            [this](const QHttpServerRequest& request) {
                lastAuthorization = request.value("Authorization");
                lastBody = requestObject(request);
                return QHttpServerResponse(lastBody);
            }
        );

        if (!tcpServer.listen(QHostAddress::LocalHost, 0) ||
            !server.bind(&tcpServer)) {
            throw TestFailure("unable to start desktop API test server");
        }
        port = tcpServer.serverPort();
    }

    QUrl baseUrl() const {
        return QUrl(QStringLiteral("http://127.0.0.1:%1/root").arg(port));
    }
};

void configurationAndBootstrapUseTheExpectedUrlAndJson(TestApi& api) {
    ApiClient client(api.baseUrl());
    require(client.baseUrl() == api.baseUrl(), "configured base URL must be retained");
    require(
        !client.setBaseUrl(QUrl(QStringLiteral("file:///unsafe/path"))),
        "non-HTTP base URL must be rejected"
    );
    require(client.baseUrl() == api.baseUrl(), "invalid URL must not replace prior URL");

    const auto response = waitFor([&](ApiClient::Callback callback) {
        client.bootstrap(
            QStringLiteral("temporary password value"),
            QStringLiteral("Administrator"),
            std::move(callback)
        );
    });
    require(response.succeeded(), "bootstrap request must succeed");
    require(response.httpStatus == 201, "bootstrap must preserve HTTP 201");
    require(
        api.lastPath == QStringLiteral("/root/api/v1/auth/bootstrap"),
        "base path and endpoint path must be joined"
    );
    require(
        api.lastBody.value(QStringLiteral("password")).toString() ==
            QStringLiteral("temporary password value"),
        "bootstrap password must be encoded as JSON"
    );
    require(
        api.lastBody.value(QStringLiteral("displayName")).toString() ==
            QStringLiteral("Administrator"),
        "bootstrap display name must be encoded as JSON"
    );
}

void loginStoresMemorySessionAndInjectsBearerToken(TestApi& api) {
    ApiClient client(api.baseUrl());
    int sessionChanges{};
    QObject::connect(&client, &ApiClient::sessionChanged, [&](bool) {
        ++sessionChanges;
    });

    const auto login = waitFor([&](ApiClient::Callback callback) {
        client.login(
            QStringLiteral("admin"),
            QStringLiteral("login password value"),
            std::move(callback)
        );
    });
    require(login.succeeded(), "login must succeed");
    require(client.isAuthenticated(), "login must create an in-memory session");
    require(
        client.session().user.value(QStringLiteral("role")).toString() ==
            QStringLiteral("admin"),
        "login must store returned user"
    );
    require(sessionChanges == 1, "login must notify session observers");

    const auto response = waitFor([&](ApiClient::Callback callback) {
        client.get(
            QStringLiteral("/api/v1/protected?search=steel"),
            std::move(callback)
        );
    });
    require(response.succeeded(), "authenticated GET must succeed");
    require(
        api.lastAuthorization == QByteArray("Bearer secret-session-token"),
        "authenticated request must inject the Bearer token"
    );
    require(
        response.body.value(QStringLiteral("query")).toString() ==
            QStringLiteral("search=steel"),
        "endpoint query must be preserved"
    );

    require(
        client.setBaseUrl(QUrl(QStringLiteral("http://127.0.0.1:18080"))),
        "a new valid base URL must be accepted"
    );
    require(
        !client.isAuthenticated(),
        "changing server must clear the old server session"
    );
    require(sessionChanges == 2, "server change must notify session observers");
}

void authWrappersRefreshAndClearSession(TestApi& api) {
    ApiClient client(api.baseUrl());
    const auto login = waitFor([&](ApiClient::Callback callback) {
        client.login(
            QStringLiteral("admin"),
            QStringLiteral("login password value"),
            std::move(callback)
        );
    });
    require(login.succeeded(), "login setup must succeed");

    const auto me = waitFor([&](ApiClient::Callback callback) {
        client.me(std::move(callback));
    });
    require(me.succeeded(), "me request must succeed");
    require(
        !client.session().user
             .value(QStringLiteral("mustChangePassword"))
             .toBool(true),
        "me must refresh the cached user"
    );

    const auto changed = waitFor([&](ApiClient::Callback callback) {
        client.changePassword(
            QStringLiteral("old password value"),
            QStringLiteral("new password value"),
            std::move(callback)
        );
    });
    require(changed.succeeded(), "change-password request must succeed");
    require(
        api.lastBody.value(QStringLiteral("currentPassword")).toString() ==
            QStringLiteral("old password value"),
        "current password must be sent in the expected field"
    );
    require(
        api.lastBody.value(QStringLiteral("newPassword")).toString() ==
            QStringLiteral("new password value"),
        "new password must be sent in the expected field"
    );

    const auto logout = waitFor([&](ApiClient::Callback callback) {
        client.logout(std::move(callback));
    });
    require(logout.succeeded(), "logout request must succeed");
    require(!client.isAuthenticated(), "successful logout must clear session");
}

void errorsAreStructuredAndOnlyUnauthorizedClearsSession(TestApi& api) {
    ApiClient client(api.baseUrl());
    const auto login = waitFor([&](ApiClient::Callback callback) {
        client.login(
            QStringLiteral("admin"),
            QStringLiteral("login password value"),
            std::move(callback)
        );
    });
    require(login.succeeded(), "login setup must succeed");

    const auto forbidden = waitFor([&](ApiClient::Callback callback) {
        client.get(QStringLiteral("/api/v1/forbidden"), std::move(callback));
    });
    require(!forbidden.succeeded(), "HTTP 403 must be an error");
    require(forbidden.error.kind == ApiErrorKind::Http, "403 error kind must be HTTP");
    require(forbidden.error.httpStatus == 403, "403 status must be preserved");
    require(
        forbidden.error.code == QStringLiteral("forbidden"),
        "server error code must be preserved"
    );
    require(client.isAuthenticated(), "HTTP 403 must retain the current session");

    const auto unauthorized = waitFor([&](ApiClient::Callback callback) {
        client.get(QStringLiteral("/api/v1/expired"), std::move(callback));
    });
    require(!unauthorized.succeeded(), "HTTP 401 must be an error");
    require(unauthorized.error.httpStatus == 401, "401 status must be preserved");
    require(
        unauthorized.error.code == QStringLiteral("session_expired"),
        "401 server error code must be preserved"
    );
    require(!client.isAuthenticated(), "HTTP 401 must clear the current session");
}

void genericPostPutAndPatchSendJson(TestApi& api) {
    ApiClient client(api.baseUrl());
    const QJsonObject createBody{
        {QStringLiteral("code"), QStringLiteral("MAT-001")},
        {QStringLiteral("enabled"), true},
    };
    const auto created = waitFor([&](ApiClient::Callback callback) {
        client.post(
            QStringLiteral("/api/v1/create"),
            createBody,
            std::move(callback)
        );
    });
    require(created.succeeded(), "generic POST must succeed");
    require(created.httpStatus == 201, "generic POST must retain status");
    require(created.body == createBody, "generic POST response JSON must be parsed");

    const QJsonObject updateBody{
        {QStringLiteral("revision"), 2},
        {QStringLiteral("name"), QStringLiteral("Steel")},
    };
    const auto updated = waitFor([&](ApiClient::Callback callback) {
        client.put(
            QStringLiteral("/api/v1/update"),
            updateBody,
            std::move(callback)
        );
    });
    require(updated.succeeded(), "generic PUT must succeed");
    require(updated.body == updateBody, "generic PUT JSON must round-trip");

    const QJsonObject patchBody{
        {QStringLiteral("enabled"), false},
        {QStringLiteral("revision"), 3},
    };
    const auto patched = waitFor([&](ApiClient::Callback callback) {
        client.patch(
            QStringLiteral("/api/v1/enabled"),
            patchBody,
            std::move(callback)
        );
    });
    require(patched.succeeded(), "generic PATCH must succeed");
    require(patched.body == patchBody, "generic PATCH JSON must round-trip");
}

void networkFailureIsReportedWithoutAFalseHttpError() {
    QTcpServer temporaryServer;
    require(
        temporaryServer.listen(QHostAddress::LocalHost, 0),
        "must reserve an unused local port"
    );
    const auto unusedPort = temporaryServer.serverPort();
    temporaryServer.close();

    ApiClient client(QUrl(
        QStringLiteral("http://127.0.0.1:%1").arg(unusedPort)
    ));
    const auto response = waitFor([&](ApiClient::Callback callback) {
        client.get(QStringLiteral("/api/v1/unreachable"), std::move(callback));
    });
    require(!response.succeeded(), "unreachable server must fail");
    require(
        response.error.kind == ApiErrorKind::Network,
        "connection refusal must be reported as a network error"
    );
    require(response.httpStatus == 0, "network failure must not invent HTTP status");
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);

    try {
        TestApi api;
        const std::vector<std::pair<std::string, std::function<void()>>> tests = {
            {
                "configuration, URL, and bootstrap JSON",
                [&]() { configurationAndBootstrapUseTheExpectedUrlAndJson(api); }
            },
            {
                "login session and Bearer injection",
                [&]() { loginStoresMemorySessionAndInjectsBearerToken(api); }
            },
            {
                "authentication wrappers",
                [&]() { authWrappersRefreshAndClearSession(api); }
            },
            {
                "structured 401 and 403 errors",
                [&]() { errorsAreStructuredAndOnlyUnauthorizedClearsSession(api); }
            },
            {
                "generic POST, PUT, and PATCH",
                [&]() { genericPostPutAndPatchSendJson(api); }
            },
            {
                "network failure",
                []() { networkFailureIsReportedWithoutAFalseHttpError(); }
            },
        };

        std::size_t passed{};
        for (const auto& [name, test] : tests) {
            try {
                test();
                ++passed;
                std::cout << "[PASS] " << name << '\n';
            } catch (const std::exception& error) {
                std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
            }
        }
        std::cout << passed << "/" << tests.size() << " tests passed\n";
        return passed == tests.size() ? EXIT_SUCCESS : EXIT_FAILURE;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] test setup: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
