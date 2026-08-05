#include "manage/server/api_server.h"

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
    const QJsonObject& object
) {
    QNetworkRequest request(endpoint(port, path));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    return waitForReply(network.sendCustomRequest(
        request,
        method,
        QJsonDocument(object).toJson(QJsonDocument::Compact)
    ));
}

QJsonObject materialPayload() {
    return {
        {QStringLiteral("code"), QStringLiteral("MAT-REST-001")},
        {QStringLiteral("name"), QStringLiteral("REST material")},
        {QStringLiteral("unit"), QStringLiteral("piece")},
        {QStringLiteral("currentUnitPriceCents"), 5'500},
    };
}

void runCatalogRouteScenario(QNetworkAccessManager& network, quint16 port) {
    const auto created = sendJson(
        network, port, QByteArrayLiteral("POST"),
        QStringLiteral("/api/v1/materials"), materialPayload()
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

    const auto listed = waitForReply(network.get(QNetworkRequest(endpoint(
        port,
        QStringLiteral("/api/v1/materials?search=REST&page=1&pageSize=10&enabled=true")
    ))));
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
        }
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
        QStringLiteral("/api/v1/materials/%1").arg(materialId), stalePayload
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
        QStringLiteral("/api/v1/customers"), customer
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
        QStringLiteral("/api/v1/customers/%1").arg(customerId), customerUpdate
    );
    require(customerUpdated.status == 200, "customer update must return HTTP 200");
    const auto updatedObject = QJsonDocument::fromJson(customerUpdated.body).object();
    require(updatedObject.value(QStringLiteral("phone")).toString() == QStringLiteral("654321"),
            "customer update response");
    require(updatedObject.value(QStringLiteral("revision")).toInteger() == 2,
            "customer revision increments");

    const auto missing = waitForReply(network.get(QNetworkRequest(endpoint(
        port, QStringLiteral("/api/v1/customers/999")
    ))));
    require(missing.status == 404, "missing customer must return HTTP 404");
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    auto repository = std::make_shared<InMemoryCatalogRepository>();
    manage::server::ApiServer server(repository);
    const auto port = server.listen(QHostAddress::LocalHost, 0);
    if (port == 0) {
        std::cerr << "[FAIL] unable to start catalog route test server\n";
        return EXIT_FAILURE;
    }

    QNetworkAccessManager network;
    try {
        runCatalogRouteScenario(network, port);
        std::cout << "[PASS] catalog REST lifecycle and errors\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] catalog routes: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
