#include "manage/server/api_server.h"

#include "manage/auth/auth_service.h"

#include "support/fake_user_repository.h"
#include "support/in_memory_catalog_repository.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

class TestFailure final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct NetworkResponse final {
    int status{};
    QByteArray body;
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
    response.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    response.body = reply->readAll();
    reply->deleteLater();
    return response;
}

QUrl endpoint(quint16 port, const QString& path) {
    return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(port).arg(path));
}

NetworkResponse sendJson(
    QNetworkAccessManager& network,
    quint16 port,
    const QByteArray& method,
    const QString& path,
    const QJsonObject& object,
    const QByteArray& bearerToken = {}
) {
    QNetworkRequest request(endpoint(port, path));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!bearerToken.isEmpty()) {
        request.setRawHeader("Authorization", "Bearer " + bearerToken);
    }
    return waitForReply(network.sendCustomRequest(
        request,
        method,
        QJsonDocument(object).toJson(QJsonDocument::Compact)
    ));
}

QNetworkRequest authorizedRequest(
    quint16 port,
    const QString& path,
    const QByteArray& bearerToken
) {
    QNetworkRequest request(endpoint(port, path));
    request.setRawHeader("Authorization", "Bearer " + bearerToken);
    return request;
}

QJsonObject materialPayload() {
    return {
        {QStringLiteral("code"), QStringLiteral("MAT-REST-001")},
        {QStringLiteral("name"), QStringLiteral("REST material")},
        {QStringLiteral("unit"), QStringLiteral("piece")},
        {QStringLiteral("currentUnitPriceCents"), 5'500},
    };
}

void runCatalogRouteScenario(
    QNetworkAccessManager& network,
    quint16 port,
    const QByteArray& bearerToken
) {
    const auto created = sendJson(
        network, port, QByteArrayLiteral("POST"),
        QStringLiteral("/api/v1/materials"), materialPayload(), bearerToken
    );
    require(created.status == 201, "material create must return HTTP 201");
    const auto createdObject = QJsonDocument::fromJson(created.body).object();
    const auto materialId = createdObject.value(QStringLiteral("id")).toInteger();
    require(materialId == 1, "created material id");
    require(createdObject.value(QStringLiteral("revision")).toInteger() == 1,
            "created material revision");
    require(createdObject.value(QStringLiteral("specification")).toString().isEmpty(),
            "omitted material specification becomes an empty string");
    require(createdObject.value(QStringLiteral("category")).toString().isEmpty(),
            "omitted material category becomes an empty string");

    const auto listed = waitForReply(network.get(authorizedRequest(
        port,
        QStringLiteral("/api/v1/materials?search=REST&page=1&pageSize=10&enabled=true"),
        bearerToken
    )));
    require(listed.status == 200, "material list must return HTTP 200");
    const auto listObject = QJsonDocument::fromJson(listed.body).object();
    require(listObject.value(QStringLiteral("total")).toInteger() == 1,
            "material list search total");
    require(listObject.value(QStringLiteral("items")).toArray().size() == 1,
            "material list item count");

    const auto disabled = sendJson(
        network, port, QByteArrayLiteral("PATCH"),
        QStringLiteral("/api/v1/materials/%1/enabled").arg(materialId),
        QJsonObject{
            {QStringLiteral("revision"), 1},
            {QStringLiteral("isEnabled"), false},
        }, bearerToken
    );
    require(disabled.status == 200, "material disable must return HTTP 200");
    const auto disabledObject = QJsonDocument::fromJson(disabled.body).object();
    require(!disabledObject.value(QStringLiteral("isEnabled")).toBool(),
            "material must be disabled instead of deleted");
    require(disabledObject.value(QStringLiteral("revision")).toInteger() == 2,
            "disable increments revision");

    auto stalePayload = materialPayload();
    stalePayload.insert(QStringLiteral("revision"), 1);
    stalePayload.insert(QStringLiteral("name"), QStringLiteral("stale update"));
    const auto stale = sendJson(
        network, port, QByteArrayLiteral("PUT"),
        QStringLiteral("/api/v1/materials/%1").arg(materialId), stalePayload,
        bearerToken
    );
    require(stale.status == 409, "stale material update must return HTTP 409");
    const auto staleObject = QJsonDocument::fromJson(stale.body).object();
    require(staleObject.value(QStringLiteral("error")).toString() ==
                QStringLiteral("revision_conflict"),
            "material conflict structured error");

    const QJsonObject customer{
        {QStringLiteral("name"), QStringLiteral("Example Customer")},
    };
    const auto customerCreated = sendJson(
        network, port, QByteArrayLiteral("POST"),
        QStringLiteral("/api/v1/customers"), customer, bearerToken
    );
    require(customerCreated.status == 201, "customer create must return HTTP 201");
    const auto customerObject = QJsonDocument::fromJson(customerCreated.body).object();
    const auto customerId = customerObject.value(QStringLiteral("id")).toInteger();
    require(customerObject.value(QStringLiteral("contactName")).toString().isEmpty(),
            "omitted customer contact becomes an empty string");
    require(customerObject.value(QStringLiteral("notes")).toString().isEmpty(),
            "omitted customer notes become an empty string");

    auto customerUpdate = customer;
    customerUpdate.insert(QStringLiteral("revision"), 1);
    customerUpdate.insert(QStringLiteral("phone"), QStringLiteral("654321"));
    const auto customerUpdated = sendJson(
        network, port, QByteArrayLiteral("PUT"),
        QStringLiteral("/api/v1/customers/%1").arg(customerId), customerUpdate,
        bearerToken
    );
    require(customerUpdated.status == 200, "customer update must return HTTP 200");
    const auto updatedObject = QJsonDocument::fromJson(customerUpdated.body).object();
    require(updatedObject.value(QStringLiteral("phone")).toString() == QStringLiteral("654321"),
            "customer update response");
    require(updatedObject.value(QStringLiteral("revision")).toInteger() == 2,
            "customer revision increments");

    const auto missing = waitForReply(network.get(authorizedRequest(
        port, QStringLiteral("/api/v1/customers/999"), bearerToken
    )));
    require(missing.status == 404, "missing customer must return HTTP 404");

    // 物料库三级向导整包创建：供应商条目使用 { "supplier": {...}, "prices": [...] } 结构。
    const QJsonObject bundlePayload{
        {QStringLiteral("material"),
         QJsonObject{
             {QStringLiteral("code"), QStringLiteral("MAT-BUNDLE-001")},
             {QStringLiteral("name"), QStringLiteral("Bundle material")},
             {QStringLiteral("unit"), QStringLiteral("meter")},
             {QStringLiteral("currentUnitPriceCents"), 12'000},
         }},
        {QStringLiteral("suppliers"),
         QJsonArray{
             QJsonObject{
                 {QStringLiteral("supplier"),
                  QJsonObject{
                      {QStringLiteral("supplierName"), QStringLiteral("Bundle Supplier")},
                      {QStringLiteral("contactName"), QStringLiteral("Contact")},
                      {QStringLiteral("phone"), QStringLiteral("123456")},
                      {QStringLiteral("leadDays"), 7},
                      {QStringLiteral("isDefault"), true},
                      {QStringLiteral("isEnabled"), true},
                  }},
                 {QStringLiteral("prices"),
                  QJsonArray{
                      QJsonObject{
                          {QStringLiteral("unitPriceCents"), 12'000},
                          {QStringLiteral("isDefault"), true},
                          {QStringLiteral("isEnabled"), true},
                      },
                  }},
             },
         }},
    };
    const auto bundleCreated = sendJson(
        network, port, QByteArrayLiteral("POST"),
        QStringLiteral("/api/v1/materials/bundle"), bundlePayload, bearerToken
    );
    require(bundleCreated.status == 201,
            "bundle create must accept nested supplier object (HTTP 201)");
    const auto bundleObject = QJsonDocument::fromJson(bundleCreated.body).object();
    require(bundleObject.value(QStringLiteral("material")).isObject(),
            "bundle response carries material");
    const auto bundleSupplierArray =
        bundleObject.value(QStringLiteral("suppliers")).toArray();
    require(bundleSupplierArray.size() == 1, "bundle response carries supplier");
    require(
        bundleSupplierArray.at(0).toObject()
            .value(QStringLiteral("supplierName")).toString() ==
            QStringLiteral("Bundle Supplier"),
        "bundle response carries supplier name"
    );

    // 缺 supplier 子对象时应被拒绝（防止扁平结构误入）。
    QJsonObject malformedBundle = bundlePayload;
    QJsonArray malformedSuppliers{
        QJsonObject{
            {QStringLiteral("supplierName"), QStringLiteral("Flat Supplier")},
            {QStringLiteral("prices"), QJsonArray{}},
        },
    };
    malformedBundle.insert(QStringLiteral("suppliers"), malformedSuppliers);
    const auto bundleRejected = sendJson(
        network, port, QByteArrayLiteral("POST"),
        QStringLiteral("/api/v1/materials/bundle"), malformedBundle, bearerToken
    );
    require(bundleRejected.status == 400,
            "bundle create must reject flat supplier objects (HTTP 400)");
}

QString responseError(const NetworkResponse& response) {
    return QJsonDocument::fromJson(response.body)
        .object()
        .value(QStringLiteral("error"))
        .toString();
}

void authorizationRulesProtectCatalog(
    QNetworkAccessManager& network,
    quint16 port,
    const std::shared_ptr<manage::auth::AuthService>& authService,
    const std::shared_ptr<manage::tests::FakeUserRepository>& userRepository
) {
    const auto bootstrap = authService->bootstrapAdministrator(
        QStringLiteral("Correct Horse Battery 1"),
        QStringLiteral("Test administrator")
    );
    require(bootstrap.succeeded(), "test administrator bootstrap");

    const auto login = authService->login(
        QStringLiteral("admin"),
        QStringLiteral("Correct Horse Battery 1")
    );
    require(login.succeeded(), "test administrator login");
    const auto token = login.session.accessToken.toLatin1();

    auto response = waitForReply(network.get(QNetworkRequest(endpoint(
        port, QStringLiteral("/api/v1/materials")
    ))));
    require(response.status == 401, "catalog request without token returns 401");
    require(responseError(response) == QStringLiteral("unauthorized"),
            "missing token has unauthorized error code");

    response = waitForReply(network.get(authorizedRequest(
        port, QStringLiteral("/api/v1/materials"), QByteArrayLiteral("wrong-token")
    )));
    require(response.status == 401, "catalog request with wrong token returns 401");

    response = waitForReply(network.get(authorizedRequest(
        port, QStringLiteral("/api/v1/materials"), token
    )));
    require(response.status == 403,
            "temporary-password session cannot access catalog");
    require(responseError(response) == QStringLiteral("password_change_required"),
            "temporary-password rejection has stable error code");

    const auto changed = authService->changePassword(
        login.session.accessToken,
        QStringLiteral("Correct Horse Battery 1"),
        QStringLiteral("Replaced Horse Battery 2")
    );
    require(changed.succeeded(), "test administrator password change");

    userRepository->setRole(manage::auth::UserRole::Viewer);
    response = waitForReply(network.get(authorizedRequest(
        port, QStringLiteral("/api/v1/materials"), token
    )));
    require(response.status == 200, "viewer can read catalog");

    response = sendJson(
        network,
        port,
        QByteArrayLiteral("POST"),
        QStringLiteral("/api/v1/materials"),
        materialPayload(),
        token
    );
    require(response.status == 403, "viewer cannot create catalog data");
    require(responseError(response) == QStringLiteral("forbidden"),
            "role rejection has forbidden error code");

    userRepository->setRole(manage::auth::UserRole::Admin);
    runCatalogRouteScenario(network, port, token);

    const auto logout = authService->logout(login.session.accessToken);
    require(logout.succeeded(), "test administrator logout");
    response = waitForReply(network.get(authorizedRequest(
        port, QStringLiteral("/api/v1/materials"), token
    )));
    require(response.status == 401, "logged-out token cannot access catalog");
}

void expiredTokenIsRejected(QNetworkAccessManager& network) {
    auto userRepository =
        std::make_shared<manage::tests::FakeUserRepository>();
    auto authService = std::make_shared<manage::auth::AuthService>(
        userRepository,
        manage::auth::PasswordHasher(
            manage::auth::PasswordHasher::kMinimumIterations
        ),
        std::chrono::seconds(0)
    );
    require(
        authService->bootstrapAdministrator(
            QStringLiteral("Correct Horse Battery 3"),
            QStringLiteral("Expiring administrator")
        ).succeeded(),
        "expiring administrator bootstrap"
    );
    const auto login = authService->login(
        QStringLiteral("admin"),
        QStringLiteral("Correct Horse Battery 3")
    );
    require(login.succeeded(), "expiring administrator login");

    auto catalogRepository = std::make_shared<InMemoryCatalogRepository>();
    manage::server::ApiServer server(authService, catalogRepository);
    const auto port = server.listen(QHostAddress::LocalHost, 0);
    require(port != 0, "expired-session test server starts");

    const auto response = waitForReply(network.get(authorizedRequest(
        port,
        QStringLiteral("/api/v1/materials"),
        login.session.accessToken.toLatin1()
    )));
    require(response.status == 401, "expired token returns HTTP 401");
    require(responseError(response) == QStringLiteral("session_expired"),
            "expired token has stable error code");
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    auto userRepository =
        std::make_shared<manage::tests::FakeUserRepository>();
    auto authService = std::make_shared<manage::auth::AuthService>(
        userRepository,
        manage::auth::PasswordHasher(
            manage::auth::PasswordHasher::kMinimumIterations
        )
    );
    auto catalogRepository = std::make_shared<InMemoryCatalogRepository>();
    manage::server::ApiServer server(authService, catalogRepository);
    const auto port = server.listen(QHostAddress::LocalHost, 0);
    if (port == 0) {
        std::cerr << "[FAIL] unable to start catalog route test server\n";
        return EXIT_FAILURE;
    }

    QNetworkAccessManager network;
    try {
        authorizationRulesProtectCatalog(
            network,
            port,
            authService,
            userRepository
        );
        std::cout << "[PASS] catalog authorization and REST lifecycle\n";
        expiredTokenIsRejected(network);
        std::cout << "[PASS] expired catalog session rejection\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] catalog routes: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
