#include "manage/auth/auth_service.h"
#include "manage/server/api_server.h"
#include "support/fake_user_repository.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QHttpServerResponder>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <cstdlib>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

class TestFailure final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct NetworkResponse final {
    int status{};
    QByteArray body;
    QString error;
};

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw TestFailure(message);
    }
}

NetworkResponse waitForReply(QNetworkReply* reply) {
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);

    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&]() {
        reply->abort();
        loop.quit();
    });

    timeout.start(5'000);
    if (!reply->isFinished()) {
        loop.exec();
    }

    NetworkResponse response;
    response.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                          .toInt();
    response.body = reply->readAll();
    if (reply->error() != QNetworkReply::NoError) {
        response.error = reply->errorString();
    }
    reply->deleteLater();
    return response;
}

QUrl endpoint(quint16 port, const QString& path) {
    return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(port).arg(path));
}

NetworkResponse postJson(
    QNetworkAccessManager& network,
    quint16 port,
    const QString& path,
    const QJsonObject& object,
    const QByteArray& bearerToken = {}
) {
    QNetworkRequest request(endpoint(port, path));
    request.setHeader(
        QNetworkRequest::ContentTypeHeader,
        QStringLiteral("application/json")
    );
    if (!bearerToken.isEmpty()) {
        request.setRawHeader("Authorization", "Bearer " + bearerToken);
    }
    return waitForReply(network.post(
        request,
        QJsonDocument(object).toJson(QJsonDocument::Compact)
    ));
}

void healthEndpointReportsServiceAndDatabaseDriver(
    QNetworkAccessManager& network,
    quint16 port
) {
    const auto response = waitForReply(network.get(QNetworkRequest(
        endpoint(port, QStringLiteral("/api/v1/health"))
    )));

    require(response.status == 200, "health endpoint must return HTTP 200");
    const auto object = QJsonDocument::fromJson(response.body).object();
    require(
        object.value(QStringLiteral("service")).toString() ==
            QStringLiteral("manage-server"),
        "health response service name"
    );
    require(
        object.value(QStringLiteral("database")).toObject().contains(
            QStringLiteral("driverAvailable")
        ),
        "health response must expose QMYSQL availability"
    );
}

void calculateEndpointUsesDomainRules(
    QNetworkAccessManager& network,
    quint16 port
) {
    const QJsonObject requestObject{
        {
            QStringLiteral("lines"),
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("materialCode"), QStringLiteral("MAT-001")},
                    {QStringLiteral("quantityMicros"), 2'500'000},
                    {QStringLiteral("unitPriceCents"), 1'234},
                },
                QJsonObject{
                    {QStringLiteral("materialCode"), QStringLiteral("MAT-002")},
                    {QStringLiteral("quantityMicros"), 1'000'000},
                    {QStringLiteral("unitPriceCents"), 500},
                },
            }
        },
        {QStringLiteral("freightCents"), 1'000},
        {QStringLiteral("otherFeesCents"), 200},
        {QStringLiteral("markupBasisPoints"), 2'000},
        {QStringLiteral("taxBasisPoints"), 1'300},
    };

    QNetworkRequest request(endpoint(
        port,
        QStringLiteral("/api/v1/quotes/calculate")
    ));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    const auto response = waitForReply(network.post(
        request,
        QJsonDocument(requestObject).toJson(QJsonDocument::Compact)
    ));
    require(response.status == 200, "calculate endpoint must return HTTP 200");

    const auto object = QJsonDocument::fromJson(response.body).object();
    require(
        object.value(QStringLiteral("materialCostCents")).toInteger() == 3'585,
        "material cost result"
    );
    require(
        object.value(QStringLiteral("priceBeforeTaxCents")).toInteger() == 5'742,
        "price before tax result"
    );
    require(
        object.value(QStringLiteral("priceWithTaxCents")).toInteger() == 6'488,
        "price with tax result"
    );
}

void calculateEndpointReturnsStructuredValidationErrors(
    QNetworkAccessManager& network,
    quint16 port
) {
    const QJsonObject requestObject{
        {
            QStringLiteral("lines"),
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("materialCode"), QStringLiteral("MAT-001")},
                    {QStringLiteral("quantityMicros"), 0},
                    {QStringLiteral("unitPriceCents"), 100},
                },
            }
        },
    };

    QNetworkRequest request(endpoint(
        port,
        QStringLiteral("/api/v1/quotes/calculate")
    ));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    const auto response = waitForReply(network.post(
        request,
        QJsonDocument(requestObject).toJson(QJsonDocument::Compact)
    ));
    require(response.status == 400, "invalid quote must return HTTP 400");

    const auto object = QJsonDocument::fromJson(response.body).object();
    require(
        object.value(QStringLiteral("error")).toString() ==
            QStringLiteral("non_positive_quantity"),
        "validation error code"
    );
    require(
        object.value(QStringLiteral("lineIndex")).toInteger() == 0,
        "validation error line index"
    );
}

void authenticationEndpointsCoverTheSessionLifecycle(
    QNetworkAccessManager& network,
    quint16 port
) {
    const auto bootstrap = postJson(
        network,
        port,
        QStringLiteral("/api/v1/auth/bootstrap"),
        QJsonObject{
            {
                QStringLiteral("password"),
                QStringLiteral("Correct Horse Battery 1")
            },
            {QStringLiteral("displayName"), QStringLiteral("系统管理员")},
        }
    );
    require(bootstrap.status == 201, "bootstrap endpoint must return HTTP 201");
    require(
        QJsonDocument::fromJson(bootstrap.body)
                .object()
                .value(QStringLiteral("user"))
                .toObject()
                .value(QStringLiteral("role"))
                .toString() == QStringLiteral("admin"),
        "bootstrap returns the admin role"
    );

    const auto login = postJson(
        network,
        port,
        QStringLiteral("/api/v1/auth/login"),
        QJsonObject{
            {QStringLiteral("username"), QStringLiteral("admin")},
            {
                QStringLiteral("password"),
                QStringLiteral("Correct Horse Battery 1")
            },
        }
    );
    require(login.status == 200, "login endpoint must return HTTP 200");
    const auto token = QJsonDocument::fromJson(login.body)
                           .object()
                           .value(QStringLiteral("accessToken"))
                           .toString()
                           .toLatin1();
    require(!token.isEmpty(), "login returns an access token");

    QNetworkRequest meRequest(endpoint(
        port,
        QStringLiteral("/api/v1/auth/me")
    ));
    meRequest.setRawHeader("Authorization", "Bearer " + token);
    const auto me = waitForReply(network.get(meRequest));
    require(me.status == 200, "me endpoint accepts an active token");
    require(
        QJsonDocument::fromJson(me.body)
            .object()
            .value(QStringLiteral("user"))
            .toObject()
            .value(QStringLiteral("mustChangePassword"))
            .toBool(),
        "first login reports required password change"
    );

    const auto rejectedChange = postJson(
        network,
        port,
        QStringLiteral("/api/v1/auth/change-password"),
        QJsonObject{
            {
                QStringLiteral("currentPassword"),
                QStringLiteral("Incorrect Current Password")
            },
            {
                QStringLiteral("newPassword"),
                QStringLiteral("Replaced Horse Battery 2")
            },
        },
        token
    );
    require(
        rejectedChange.status == 401,
        "incorrect current password returns HTTP 401"
    );
    require(
        QJsonDocument::fromJson(rejectedChange.body)
                .object()
                .value(QStringLiteral("error"))
                .toString() == QStringLiteral("invalid_credentials"),
        "password change error contains a stable code"
    );

    const auto changed = postJson(
        network,
        port,
        QStringLiteral("/api/v1/auth/change-password"),
        QJsonObject{
            {
                QStringLiteral("currentPassword"),
                QStringLiteral("Correct Horse Battery 1")
            },
            {
                QStringLiteral("newPassword"),
                QStringLiteral("Replaced Horse Battery 2")
            },
        },
        token
    );
    require(changed.status == 200, "change-password endpoint returns HTTP 200");
    require(
        !QJsonDocument::fromJson(changed.body)
             .object()
             .value(QStringLiteral("user"))
             .toObject()
             .value(QStringLiteral("mustChangePassword"))
             .toBool(true),
        "password change clears required flag"
    );

    const auto logout = postJson(
        network,
        port,
        QStringLiteral("/api/v1/auth/logout"),
        QJsonObject{},
        token
    );
    require(logout.status == 200, "logout endpoint must return HTTP 200");

    QNetworkRequest reusedRequest(endpoint(
        port,
        QStringLiteral("/api/v1/auth/me")
    ));
    reusedRequest.setRawHeader("Authorization", "Bearer " + token);
    const auto reused = waitForReply(network.get(reusedRequest));
    require(reused.status == 401, "logged-out token must return HTTP 401");
    require(
        QJsonDocument::fromJson(reused.body)
                .object()
                .value(QStringLiteral("error"))
                .toString() == QStringLiteral("unauthorized"),
        "authentication error contains a stable code"
    );
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);

    auto repository = std::make_shared<manage::tests::FakeUserRepository>();
    auto authService = std::make_shared<manage::auth::AuthService>(
        repository,
        manage::auth::PasswordHasher(
            manage::auth::PasswordHasher::kMinimumIterations
        )
    );
    manage::server::ApiServer server(std::move(authService));
    const auto port = server.listen(QHostAddress::LocalHost, 0);
    if (port == 0) {
        std::cerr << "[FAIL] unable to start test server\n";
        return EXIT_FAILURE;
    }

    QNetworkAccessManager network;
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {
            "health endpoint",
            [&]() { healthEndpointReportsServiceAndDatabaseDriver(network, port); }
        },
        {
            "calculate endpoint",
            [&]() { calculateEndpointUsesDomainRules(network, port); }
        },
        {
            "validation errors",
            [&]() {
                calculateEndpointReturnsStructuredValidationErrors(network, port);
            }
        },
        {
            "authentication lifecycle",
            [&]() {
                authenticationEndpointsCoverTheSessionLifecycle(network, port);
            }
        },
    };

    std::size_t passed = 0;
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
}
