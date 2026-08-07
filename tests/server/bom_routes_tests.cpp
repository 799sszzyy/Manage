#include "manage/auth/auth_service.h"
#include "manage/data/bom_repository.h"
#include "manage/data/bom_service.h"
#include "manage/server/api_server.h"

#include "support/fake_user_repository.h"

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
#include <optional>
#include <stdexcept>
#include <string>

namespace {

class TestFailure final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw TestFailure(message);
    }
}

class MemoryBomRepository final : public manage::data::BomRepository {
public:
    std::optional<manage::data::BomTemplate> stored;

    manage::data::BomRepositoryStatus list(
        const manage::data::BomSearchQuery& query,
        manage::data::BomPage& result,
        QString&
    ) override {
        result.page = query.page;
        result.pageSize = query.pageSize;
        result.total = stored.has_value() ? 1 : 0;
        if (stored.has_value()) {
            result.items.push_back(stored->summary);
        }
        return manage::data::BomRepositoryStatus::Success;
    }

    manage::data::BomRepositoryStatus getById(
        qint64 id,
        std::optional<manage::data::BomTemplate>& result,
        QString&
    ) override {
        if (!stored.has_value() || stored->summary.id != id) {
            result.reset();
            return manage::data::BomRepositoryStatus::NotFound;
        }
        result = stored;
        return manage::data::BomRepositoryStatus::Success;
    }

    manage::data::BomRepositoryStatus lookupMaterials(
        const std::vector<qint64>& ids,
        std::vector<manage::data::MaterialReference>& result,
        QString&
    ) override {
        result.clear();
        for (const auto id : ids) {
            if (id == 42) {
                result.push_back({42, true});
            }
        }
        return manage::data::BomRepositoryStatus::Success;
    }

    manage::data::BomRepositoryStatus create(
        const manage::data::NewBomTemplate& command,
        manage::data::BomTemplate& result,
        QString&
    ) override {
        stored = makeTemplate(command.code, command.name, command.description,
                              command.isEnabled, 1, command.items);
        result = *stored;
        return manage::data::BomRepositoryStatus::Success;
    }

    manage::data::BomRepositoryStatus update(
        const manage::data::UpdateBomTemplate& command,
        manage::data::BomTemplate& result,
        QString&
    ) override {
        if (!matches(command.id, command.expectedRevision)) {
            return manage::data::BomRepositoryStatus::Conflict;
        }
        stored->summary.code = command.code;
        stored->summary.name = command.name;
        stored->summary.description = command.description;
        ++stored->summary.revision;
        result = *stored;
        return manage::data::BomRepositoryStatus::Success;
    }

    manage::data::BomRepositoryStatus setEnabled(
        const manage::data::SetBomEnabled& command,
        manage::data::BomTemplate& result,
        QString&
    ) override {
        if (!matches(command.id, command.expectedRevision)) {
            return manage::data::BomRepositoryStatus::Conflict;
        }
        stored->summary.isEnabled = command.isEnabled;
        ++stored->summary.revision;
        result = *stored;
        return manage::data::BomRepositoryStatus::Success;
    }

    manage::data::BomRepositoryStatus replaceItems(
        const manage::data::ReplaceBomItems& command,
        manage::data::BomTemplate& result,
        QString&
    ) override {
        if (!matches(command.id, command.expectedRevision)) {
            return manage::data::BomRepositoryStatus::Conflict;
        }
        stored->items = makeItems(command.items);
        ++stored->summary.revision;
        result = *stored;
        return manage::data::BomRepositoryStatus::Success;
    }

private:
    static std::vector<manage::data::BomItem> makeItems(
        const std::vector<manage::data::BomItemInput>& inputs
    ) {
        std::vector<manage::data::BomItem> items;
        for (const auto& input : inputs) {
            const auto hasSupplier = input.materialSupplierId > 0;
            items.push_back({
                static_cast<qint64>(items.size() + 1), input.lineNo,
                input.materialId, QStringLiteral("MAT-42"),
                QStringLiteral("Test material"), QString(),
                QStringLiteral("piece"), input.quantityMicros, input.notes,
                input.materialSupplierId,
                hasSupplier ? QStringLiteral("测试供应商") : QString{},
                input.copperPriceCents,
                hasSupplier ? 1234 : 0,
            });
        }
        return items;
    }

    static manage::data::BomTemplate makeTemplate(
        const QString& code,
        const QString& name,
        const QString& description,
        bool enabled,
        int revision,
        const std::vector<manage::data::BomItemInput>& items
    ) {
        return {{1, code, name, description, enabled, revision}, makeItems(items)};
    }

    bool matches(qint64 id, int revision) const {
        return stored.has_value() && stored->summary.id == id &&
               stored->summary.revision == revision;
    }
};

struct NetworkResponse final {
    int status{};
    QByteArray body;
};

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
    const NetworkResponse response{
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),
        reply->readAll(),
    };
    reply->deleteLater();
    return response;
}

QUrl endpoint(quint16 port, const QString& path) {
    return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(port).arg(path));
}

QNetworkRequest jsonRequest(
    quint16 port,
    const QString& path,
    const QByteArray& bearerToken
) {
    QNetworkRequest request(endpoint(port, path));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    request.setRawHeader("Authorization", "Bearer " + bearerToken);
    return request;
}

QByteArray compact(const QJsonObject& object) {
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

void completeBomRouteFlow(
    QNetworkAccessManager& network,
    quint16 port,
    const QByteArray& bearerToken
) {
    const QJsonArray initialItems{
        QJsonObject{{QStringLiteral("lineNo"), 10},
                    {QStringLiteral("materialId"), 42},
                    {QStringLiteral("quantityMicros"), 2'000'000}},
    };
    const QJsonObject createBody{
        {QStringLiteral("code"), QStringLiteral("BOM-HTTP")},
        {QStringLiteral("name"), QStringLiteral("HTTP assembly")},
        {QStringLiteral("items"), initialItems},
    };
    auto response = waitForReply(network.post(
        jsonRequest(port, QStringLiteral("/api/v1/boms"), bearerToken),
        compact(createBody)
    ));
    require(response.status == 201, "create BOM must return HTTP 201");
    auto object = QJsonDocument::fromJson(response.body).object();
    require(object.value(QStringLiteral("revision")).toInteger() == 1,
            "created BOM revision");
    require(object.value(QStringLiteral("description")).toString().isEmpty(),
            "omitted description is returned as empty text");
    require(object.value(QStringLiteral("items")).toArray().first().toObject()
                .value(QStringLiteral("notes")).toString().isEmpty(),
            "omitted item notes are returned as empty text");

    const QJsonObject updateBody{
        {QStringLiteral("code"), QStringLiteral("BOM-HTTP")},
        {QStringLiteral("name"), QStringLiteral("Updated HTTP assembly")},
        {QStringLiteral("description"), QStringLiteral("updated")},
        {QStringLiteral("revision"), 1},
    };
    response = waitForReply(network.put(
        jsonRequest(port, QStringLiteral("/api/v1/boms/1"), bearerToken),
        compact(updateBody)
    ));
    require(response.status == 200, "update BOM must return HTTP 200");
    object = QJsonDocument::fromJson(response.body).object();
    require(object.value(QStringLiteral("revision")).toInteger() == 2,
            "metadata update increases revision");
    require(object.value(QStringLiteral("name")).toString() ==
                QStringLiteral("Updated HTTP assembly"),
            "updated name returned");

    const QJsonObject replaceBody{
        {QStringLiteral("revision"), 2},
        {QStringLiteral("items"), QJsonArray{
            QJsonObject{{QStringLiteral("lineNo"), 20},
                        {QStringLiteral("materialId"), 42},
                        {QStringLiteral("quantityMicros"), 3'000'000}},
        }},
    };
    response = waitForReply(network.put(
        jsonRequest(
            port,
            QStringLiteral("/api/v1/boms/1/items"),
            bearerToken
        ),
        compact(replaceBody)
    ));
    require(response.status == 200, "replace items must return HTTP 200");
    object = QJsonDocument::fromJson(response.body).object();
    require(object.value(QStringLiteral("revision")).toInteger() == 3,
            "replacement increases revision");
    require(object.value(QStringLiteral("items")).toArray().first()
                .toObject().value(QStringLiteral("quantityMicros")).toInteger() == 3'000'000,
            "replacement quantity returned");

    const QJsonObject disableBody{
        {QStringLiteral("revision"), 3},
        {QStringLiteral("isEnabled"), false},
    };
    response = waitForReply(network.sendCustomRequest(
        jsonRequest(
            port,
            QStringLiteral("/api/v1/boms/1/enabled"),
            bearerToken
        ),
        QByteArrayLiteral("PATCH"), compact(disableBody)
    ));
    require(response.status == 200, "disable must return HTTP 200");
    object = QJsonDocument::fromJson(response.body).object();
    require(!object.value(QStringLiteral("isEnabled")).toBool(),
            "disabled state returned");
    require(object.value(QStringLiteral("revision")).toInteger() == 4,
            "disable increases revision");

    response = waitForReply(network.get(jsonRequest(
        port,
        QStringLiteral("/api/v1/boms/1"),
        bearerToken
    )));
    require(response.status == 200, "detail must return HTTP 200");
    object = QJsonDocument::fromJson(response.body).object();
    require(object.value(QStringLiteral("items")).toArray().size() == 1,
            "detail returns BOM items");

    response = waitForReply(network.get(jsonRequest(
        port,
        QStringLiteral("/api/v1/boms?page=1&pageSize=10"),
        bearerToken
    )));
    require(response.status == 200, "list must return HTTP 200");
    object = QJsonDocument::fromJson(response.body).object();
    require(object.value(QStringLiteral("total")).toInteger() == 1,
            "list returns total");
}

void malformedAndDuplicateItemsAreRejected(
    QNetworkAccessManager& network,
    quint16 port,
    const QByteArray& bearerToken
) {
    const QJsonObject body{
        {QStringLiteral("code"), QStringLiteral("BOM-BAD")},
        {QStringLiteral("name"), QStringLiteral("Bad BOM")},
        {QStringLiteral("items"), QJsonArray{
            QJsonObject{{QStringLiteral("lineNo"), 10},
                        {QStringLiteral("materialId"), 42},
                        {QStringLiteral("quantityMicros"), 1}},
            QJsonObject{{QStringLiteral("lineNo"), 20},
                        {QStringLiteral("materialId"), 42},
                        {QStringLiteral("quantityMicros"), 1}},
        }},
    };
    const auto response = waitForReply(network.post(
        jsonRequest(port, QStringLiteral("/api/v1/boms"), bearerToken),
        compact(body)
    ));
    require(response.status == 400, "duplicate material must return HTTP 400");
    const auto object = QJsonDocument::fromJson(response.body).object();
    require(object.value(QStringLiteral("error")).toString() ==
                QStringLiteral("invalid_request"),
            "validation error code");
}

// 批次9：BOM 条目携带供应商与铜价档；负供应商 id / 非正铜价被拒绝。
void bomItemsCarrySupplierAndCopperTier(
    QNetworkAccessManager& network,
    quint16 port,
    const QByteArray& bearerToken
) {
    const QJsonObject body{
        {QStringLiteral("code"), QStringLiteral("BOM-SUP")},
        {QStringLiteral("name"), QStringLiteral("Supplier BOM")},
        {QStringLiteral("items"), QJsonArray{
            QJsonObject{{QStringLiteral("lineNo"), 10},
                        {QStringLiteral("materialId"), 42},
                        {QStringLiteral("quantityMicros"), 1'000'000},
                        {QStringLiteral("materialSupplierId"), 7},
                        {QStringLiteral("copperPriceCents"), 7'000'000}},
        }},
    };
    const auto response = waitForReply(network.post(
        jsonRequest(port, QStringLiteral("/api/v1/boms"), bearerToken),
        compact(body)
    ));
    require(response.status == 201, "BOM with supplier fields must be created");
    const auto object = QJsonDocument::fromJson(response.body).object();
    const auto item = object.value(QStringLiteral("items")).toArray().first().toObject();
    require(item.value(QStringLiteral("materialSupplierId")).toInteger() == 7,
            "BOM item must echo materialSupplierId");
    require(item.value(QStringLiteral("supplierName")).toString() ==
                QStringLiteral("测试供应商"),
            "BOM item must echo supplier name snapshot");
    require(item.value(QStringLiteral("copperPriceCents")).toInteger() == 7'000'000,
            "BOM item must echo copper tier snapshot");
    require(item.value(QStringLiteral("unitPriceCents")).toInteger() == 1234,
            "BOM item must echo resolved unit price snapshot");

    const QJsonObject negativeSupplier{
        {QStringLiteral("code"), QStringLiteral("BOM-NEG")},
        {QStringLiteral("name"), QStringLiteral("Negative supplier")},
        {QStringLiteral("items"), QJsonArray{
            QJsonObject{{QStringLiteral("lineNo"), 10},
                        {QStringLiteral("materialId"), 42},
                        {QStringLiteral("quantityMicros"), 1},
                        {QStringLiteral("materialSupplierId"), -1}},
        }},
    };
    const auto rejected = waitForReply(network.post(
        jsonRequest(port, QStringLiteral("/api/v1/boms"), bearerToken),
        compact(negativeSupplier)
    ));
    require(rejected.status == 400,
            "negative materialSupplierId must return HTTP 400");

    const QJsonObject zeroCopper{
        {QStringLiteral("code"), QStringLiteral("BOM-COPPER")},
        {QStringLiteral("name"), QStringLiteral("Zero copper")},
        {QStringLiteral("items"), QJsonArray{
            QJsonObject{{QStringLiteral("lineNo"), 10},
                        {QStringLiteral("materialId"), 42},
                        {QStringLiteral("quantityMicros"), 1},
                        {QStringLiteral("copperPriceCents"), 0}},
        }},
    };
    const auto copperRejected = waitForReply(network.post(
        jsonRequest(port, QStringLiteral("/api/v1/boms"), bearerToken),
        compact(zeroCopper)
    ));
    require(copperRejected.status == 400,
            "non-positive copperPriceCents must return HTTP 400");
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    MemoryBomRepository repository;
    manage::data::BomService service(repository);
    auto userRepository =
        std::make_shared<manage::tests::FakeUserRepository>();
    auto authService = std::make_shared<manage::auth::AuthService>(
        userRepository,
        manage::auth::PasswordHasher(
            manage::auth::PasswordHasher::kMinimumIterations
        )
    );
    require(
        authService->bootstrapAdministrator(
            QStringLiteral("Correct Horse Battery 1"),
            QStringLiteral("BOM administrator")
        ).succeeded(),
        "BOM administrator bootstrap"
    );
    const auto login = authService->login(
        QStringLiteral("admin"),
        QStringLiteral("Correct Horse Battery 1")
    );
    require(login.succeeded(), "BOM administrator login");
    require(
        authService->changePassword(
            login.session.accessToken,
            QStringLiteral("Correct Horse Battery 1"),
            QStringLiteral("Replaced Horse Battery 2")
        ).succeeded(),
        "BOM administrator password change"
    );

    manage::server::ApiServer server(authService, nullptr, &service);
    const auto port = server.listen(QHostAddress::LocalHost, 0);
    if (port == 0) {
        std::cerr << "[FAIL] unable to start BOM route test server\n";
        return EXIT_FAILURE;
    }

    QNetworkAccessManager network;
    try {
        const auto withoutToken = waitForReply(network.get(QNetworkRequest(
            endpoint(port, QStringLiteral("/api/v1/boms"))
        )));
        require(withoutToken.status == 401,
                "BOM request without token must return HTTP 401");

        const auto token = login.session.accessToken.toLatin1();
        completeBomRouteFlow(network, port, token);
        std::cout << "[PASS] complete BOM route flow\n";
        malformedAndDuplicateItemsAreRejected(network, port, token);
        std::cout << "[PASS] BOM route validation\n";
        bomItemsCarrySupplierAndCopperTier(network, port, token);
        std::cout << "[PASS] BOM supplier and copper tier fields\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
