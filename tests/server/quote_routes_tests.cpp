#include "manage/server/api_server.h"

#include "manage/auth/auth_service.h"
#include "manage/data/quote_lifecycle.h"

#include "support/fake_user_repository.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QHttpServerRequest>
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
#include <utility>

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

manage::data::QuoteDocument sampleDocument(
    qint64 id = 7,
    manage::data::QuoteStatus status = manage::data::QuoteStatus::Draft
) {
    const auto createdAt = QDateTime::fromString(
        QStringLiteral("2026-08-05T09:10:11.123Z"), Qt::ISODateWithMs
    );
    const auto updatedAt = QDateTime::fromString(
        QStringLiteral("2026-08-05T10:11:12.456Z"), Qt::ISODateWithMs
    );
    manage::data::QuoteDocument document;
    document.summary = {
        id,
        QStringLiteral("Q-20260805-%1").arg(id),
        44,
        QStringLiteral("Snapshot Customer"),
        9,
        status,
        18'360,
        3,
        createdAt,
        updatedAt,
    };
    document.customerContact = QStringLiteral("Snapshot Contact");
    document.customerPhone = QStringLiteral("123456");
    document.customerAddress = QStringLiteral("Snapshot Address");
    document.materialCostCents = 12'340;
    document.freightCents = 1'000;
    document.otherFeesCents = 200;
    document.markupBasisPoints = 2'000;
    document.markupAmountCents = 2'708;
    document.priceBeforeTaxCents = 16'248;
    document.taxBasisPoints = 1'300;
    document.taxAmountCents = 2'112;
    document.priceWithTaxCents = 18'360;
    document.notes = QStringLiteral("document notes");
    document.createdBy = 1;
    document.updatedBy = 2;
    if (status == manage::data::QuoteStatus::Issued) {
        document.issuedAt = updatedAt;
    }
    if (status == manage::data::QuoteStatus::Void) {
        document.issuedAt = createdAt;
        document.voidedAt = updatedAt;
    }
    document.items.push_back({
        71,
        1,
        55,
        QStringLiteral("MAT-055"),
        QStringLiteral("Snapshot Material"),
        QStringLiteral("10 mm"),
        QStringLiteral("piece"),
        2'500'000,
        4'936,
        12'340,
        QStringLiteral("line notes"),
    });
    return document;
}

class FakeQuoteLifecycle final : public manage::data::QuoteLifecycle {
public:
    manage::data::QuoteResult<manage::data::QuotePage> list(
        manage::data::QuoteSearchQuery query
    ) override {
        ++listCalls;
        lastQuery = std::move(query);
        if (nextError.has_value()) {
            const auto error = *nextError;
            nextError.reset();
            return manage::data::QuoteResult<manage::data::QuotePage>::failure(
                error, QStringLiteral("fake failure")
            );
        }
        manage::data::QuotePage page;
        page.items.push_back(sampleDocument().summary);
        page.total = 41;
        page.page = lastQuery.page;
        page.pageSize = lastQuery.pageSize;
        return manage::data::QuoteResult<manage::data::QuotePage>::success(
            std::move(page)
        );
    }

    manage::data::QuoteResult<manage::data::QuoteDocument> getById(
        qint64 id
    ) override {
        ++getCalls;
        lastId = id;
        if (nextError.has_value()) {
            const auto error = *nextError;
            nextError.reset();
            return manage::data::QuoteResult<manage::data::QuoteDocument>::failure(
                error, QStringLiteral("fake failure")
            );
        }
        return manage::data::QuoteResult<manage::data::QuoteDocument>::success(
            sampleDocument(id)
        );
    }

    manage::data::QuoteResult<manage::data::QuoteDocument> create(
        manage::data::CreateQuoteCommand command
    ) override {
        ++createCalls;
        lastCreate = std::move(command);
        return resultOr(sampleDocument());
    }

    manage::data::QuoteResult<manage::data::QuoteDocument> update(
        manage::data::UpdateQuoteCommand command
    ) override {
        ++updateCalls;
        lastUpdate = std::move(command);
        auto document = sampleDocument(lastUpdate.id);
        document.summary.revision = lastUpdate.expectedRevision + 1;
        return resultOr(std::move(document));
    }

    manage::data::QuoteResult<manage::data::QuoteDocument> changeStatus(
        manage::data::ChangeQuoteStatusCommand command
    ) override {
        ++statusCalls;
        lastStatus = command;
        return resultOr(sampleDocument(command.id, command.targetStatus));
    }

    manage::data::QuoteResult<manage::data::QuoteDocument> clone(
        manage::data::CloneQuoteCommand command
    ) override {
        ++cloneCalls;
        lastClone = command;
        auto document = sampleDocument(8);
        document.summary.status = manage::data::QuoteStatus::Draft;
        document.sourceQuoteId = command.sourceId;
        return resultOr(std::move(document));
    }

    manage::data::QuoteResult<bool> deleteDraft(
        manage::data::DeleteDraftQuoteCommand command
    ) override {
        ++deleteCalls;
        lastDelete = command;
        if (nextError.has_value()) {
            const auto error = *nextError;
            nextError.reset();
            return manage::data::QuoteResult<bool>::failure(
                error, QStringLiteral("fake failure")
            );
        }
        return manage::data::QuoteResult<bool>::success(true);
    }

    void failNext(manage::data::QuoteErrorCode error) { nextError = error; }

    int listCalls{};
    int getCalls{};
    int createCalls{};
    int updateCalls{};
    int statusCalls{};
    int cloneCalls{};
    int deleteCalls{};
    qint64 lastId{};
    manage::data::QuoteSearchQuery lastQuery;
    manage::data::CreateQuoteCommand lastCreate;
    manage::data::UpdateQuoteCommand lastUpdate;
    manage::data::ChangeQuoteStatusCommand lastStatus;
    manage::data::CloneQuoteCommand lastClone;
    manage::data::DeleteDraftQuoteCommand lastDelete;

private:
    manage::data::QuoteResult<manage::data::QuoteDocument> resultOr(
        manage::data::QuoteDocument document
    ) {
        if (nextError.has_value()) {
            const auto error = *nextError;
            nextError.reset();
            return manage::data::QuoteResult<manage::data::QuoteDocument>::failure(
                error, QStringLiteral("fake failure")
            );
        }
        return manage::data::QuoteResult<manage::data::QuoteDocument>::success(
            std::move(document)
        );
    }

    std::optional<manage::data::QuoteErrorCode> nextError;
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

QNetworkRequest request(
    quint16 port,
    const QString& path,
    const QByteArray& bearerToken = {}
) {
    QNetworkRequest result(endpoint(port, path));
    result.setHeader(
        QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json")
    );
    if (!bearerToken.isEmpty()) {
        result.setRawHeader("Authorization", "Bearer " + bearerToken);
    }
    return result;
}

NetworkResponse sendJson(
    QNetworkAccessManager& network,
    quint16 port,
    const QByteArray& method,
    const QString& path,
    const QJsonObject& body,
    const QByteArray& token
) {
    return waitForReply(network.sendCustomRequest(
        request(port, path, token),
        method,
        QJsonDocument(body).toJson(QJsonDocument::Compact)
    ));
}

QJsonObject responseObject(const NetworkResponse& response) {
    return QJsonDocument::fromJson(response.body).object();
}

QString responseError(const NetworkResponse& response) {
    return responseObject(response).value(QStringLiteral("error")).toString();
}

QJsonObject draftPayload() {
    return {
        {QStringLiteral("customerId"), 44},
        {QStringLiteral("bomTemplateId"), 9},
        {QStringLiteral("freightCents"), 1'000},
        {QStringLiteral("otherFeesCents"), 200},
        {QStringLiteral("markupBasisPoints"), 2'000},
        {QStringLiteral("taxBasisPoints"), 1'300},
        {QStringLiteral("notes"), QStringLiteral("request notes")},
        {QStringLiteral("customerName"), QStringLiteral("FORGED CUSTOMER")},
        {QStringLiteral("priceWithTaxCents"), 1},
        {QStringLiteral("items"), QJsonArray{
            QJsonObject{
                {QStringLiteral("materialId"), 55},
                {QStringLiteral("quantityMicros"), 2'500'000},
                {QStringLiteral("unitPriceCents"), 4'936},
                {QStringLiteral("notes"), QStringLiteral("request line")},
                {QStringLiteral("materialName"), QStringLiteral("FORGED MATERIAL")},
                {QStringLiteral("subtotalCents"), 1},
            },
        }},
    };
}

void verifySummaryJson(const QJsonObject& object) {
    require(object.value(QStringLiteral("id")).toInteger() == 7, "summary id");
    require(
        object.value(QStringLiteral("quoteNumber")).toString() ==
            QStringLiteral("Q-20260805-7"),
        "quoteNumber camelCase JSON"
    );
    require(
        object.value(QStringLiteral("customerId")).toInteger() == 44,
        "customerId camelCase JSON"
    );
    require(
        object.value(QStringLiteral("customerName")).toString() ==
            QStringLiteral("Snapshot Customer"),
        "server snapshot returned instead of forged customer"
    );
    require(
        object.value(QStringLiteral("bomTemplateId")).toInteger() == 9,
        "bomTemplateId JSON"
    );
    require(
        object.value(QStringLiteral("status")).toString() ==
            QStringLiteral("draft"),
        "status code JSON"
    );
    require(
        object.value(QStringLiteral("priceWithTaxCents")).toInteger() == 18'360,
        "calculated total returned instead of forged total"
    );
    require(object.value(QStringLiteral("revision")).toInteger() == 3,
            "revision JSON");
    require(object.value(QStringLiteral("createdAt")).isString(),
            "createdAt ISO JSON");
    require(object.value(QStringLiteral("updatedAt")).isString(),
            "updatedAt ISO JSON");
}

void verifyDocumentJson(const QJsonObject& object) {
    verifySummaryJson(object);
    const auto expectedKeys = {
        QStringLiteral("customerContact"),
        QStringLiteral("customerPhone"),
        QStringLiteral("customerAddress"),
        QStringLiteral("materialCostCents"),
        QStringLiteral("freightCents"),
        QStringLiteral("otherFeesCents"),
        QStringLiteral("markupBasisPoints"),
        QStringLiteral("markupAmountCents"),
        QStringLiteral("priceBeforeTaxCents"),
        QStringLiteral("taxBasisPoints"),
        QStringLiteral("taxAmountCents"),
        QStringLiteral("priceWithTaxCents"),
        QStringLiteral("notes"),
        QStringLiteral("sourceQuoteId"),
        QStringLiteral("createdBy"),
        QStringLiteral("updatedBy"),
        QStringLiteral("issuedAt"),
        QStringLiteral("voidedAt"),
        QStringLiteral("items"),
    };
    for (const auto& key : expectedKeys) {
        require(
            object.contains(key),
            (QStringLiteral("document JSON missing ") + key).toStdString()
        );
    }
    require(object.value(QStringLiteral("issuedAt")).isNull(),
            "unset issuedAt is JSON null");
    require(object.value(QStringLiteral("voidedAt")).isNull(),
            "unset voidedAt is JSON null");
    require(object.value(QStringLiteral("sourceQuoteId")).isNull(),
            "unset sourceQuoteId is JSON null");

    const auto items = object.value(QStringLiteral("items")).toArray();
    require(items.size() == 1, "document snapshot item count");
    const auto item = items.first().toObject();
    const auto itemKeys = {
        QStringLiteral("id"), QStringLiteral("lineNo"),
        QStringLiteral("materialId"), QStringLiteral("materialCode"),
        QStringLiteral("materialName"), QStringLiteral("specification"),
        QStringLiteral("unit"), QStringLiteral("quantityMicros"),
        QStringLiteral("unitPriceCents"), QStringLiteral("subtotalCents"),
        QStringLiteral("notes"),
    };
    for (const auto& key : itemKeys) {
        require(
            item.contains(key),
            (QStringLiteral("snapshot item JSON missing ") + key).toStdString()
        );
    }
    require(
        item.value(QStringLiteral("materialName")).toString() ==
            QStringLiteral("Snapshot Material"),
        "server material snapshot returned instead of forged material"
    );
    require(
        item.value(QStringLiteral("subtotalCents")).toInteger() == 12'340,
        "server subtotal returned instead of forged subtotal"
    );
}

void checkAuthorization(
    QNetworkAccessManager& network,
    quint16 port,
    const QByteArray& token,
    const std::shared_ptr<manage::auth::AuthService>& authService,
    const std::shared_ptr<manage::tests::FakeUserRepository>& users,
    FakeQuoteLifecycle& lifecycle
) {
    auto response = waitForReply(network.get(request(
        port, QStringLiteral("/api/v1/quotes")
    )));
    require(response.status == 401, "quote read without token returns 401");

    response = waitForReply(network.get(request(
        port, QStringLiteral("/api/v1/quotes"), token
    )));
    require(response.status == 403,
            "temporary-password account cannot read quotes");
    require(responseError(response) == QStringLiteral("password_change_required"),
            "temporary-password stable error code");

    const auto changed = authService->changePassword(
        QString::fromLatin1(token),
        QStringLiteral("Correct Horse Battery 1"),
        QStringLiteral("Replaced Horse Battery 2")
    );
    require(changed.succeeded(), "password changed for quote route testing");
    const auto activeToken = token;

    users->setRole(manage::auth::UserRole::Viewer);
    response = waitForReply(network.get(request(
        port, QStringLiteral("/api/v1/quotes/7"), activeToken
    )));
    require(response.status == 200, "viewer can read quote detail");
    const auto createsBefore = lifecycle.createCalls;
    response = sendJson(
        network, port, QByteArrayLiteral("POST"),
        QStringLiteral("/api/v1/quotes"), draftPayload(), activeToken
    );
    require(response.status == 403, "viewer cannot create quote");
    require(responseError(response) == QStringLiteral("forbidden"),
            "viewer receives forbidden error");
    require(lifecycle.createCalls == createsBefore,
            "viewer request never reaches quote lifecycle");

    users->setRole(manage::auth::UserRole::Quoter);
    response = sendJson(
        network, port, QByteArrayLiteral("POST"),
        QStringLiteral("/api/v1/quotes"), draftPayload(), activeToken
    );
    require(response.status == 201, "quoter can create quote");
    require(lifecycle.lastCreate.actorUserId == 1,
            "quoter actor id comes from authorized session");

    users->setRole(manage::auth::UserRole::Admin);
}

void checkReadAndWriteRoutes(
    QNetworkAccessManager& network,
    quint16 port,
    const QByteArray& token,
    FakeQuoteLifecycle& lifecycle
) {
    auto response = waitForReply(network.get(request(
        port,
        QStringLiteral(
            "/api/v1/quotes?page=2&pageSize=25&search=Acme%20Q&status=issued&customerId=44"
        ),
        token
    )));
    require(response.status == 200, "filtered quote list returns 200");
    require(lifecycle.lastQuery.page == 2, "page query forwarded");
    require(lifecycle.lastQuery.pageSize == 25, "pageSize query forwarded");
    require(lifecycle.lastQuery.search == QStringLiteral("Acme Q"),
            "search query forwarded and decoded");
    require(lifecycle.lastQuery.status == manage::data::QuoteStatus::Issued,
            "status query forwarded");
    require(lifecycle.lastQuery.customerId == 44,
            "customerId query forwarded");
    auto object = responseObject(response);
    require(object.value(QStringLiteral("total")).toInteger() == 41,
            "page total JSON");
    require(object.value(QStringLiteral("page")).toInteger() == 2,
            "page number JSON");
    require(object.value(QStringLiteral("pageSize")).toInteger() == 25,
            "page size JSON");
    const auto summaries = object.value(QStringLiteral("items")).toArray();
    require(summaries.size() == 1, "page items JSON");
    verifySummaryJson(summaries.first().toObject());

    response = waitForReply(network.get(request(
        port, QStringLiteral("/api/v1/quotes/7"), token
    )));
    require(response.status == 200, "quote detail returns 200");
    verifyDocumentJson(responseObject(response));

    response = sendJson(
        network, port, QByteArrayLiteral("POST"),
        QStringLiteral("/api/v1/quotes"), draftPayload(), token
    );
    require(response.status == 201, "quote create returns 201");
    require(lifecycle.lastCreate.actorUserId == 1,
            "create actor comes from session");
    require(lifecycle.lastCreate.draft.customerId == 44,
            "create customerId parsed");
    require(lifecycle.lastCreate.draft.bomTemplateId == 9,
            "create BOM id parsed");
    require(lifecycle.lastCreate.draft.items.size() == 1,
            "create lines parsed");
    require(lifecycle.lastCreate.draft.items.front().materialId == 55,
            "create material id parsed");
    verifyDocumentJson(responseObject(response));

    auto updateBody = draftPayload();
    updateBody.insert(QStringLiteral("revision"), 3);
    updateBody.insert(QStringLiteral("customerId"), 45);
    response = sendJson(
        network, port, QByteArrayLiteral("PUT"),
        QStringLiteral("/api/v1/quotes/7"), updateBody, token
    );
    require(response.status == 200, "quote update returns 200");
    require(lifecycle.lastUpdate.id == 7, "update quote id forwarded");
    require(lifecycle.lastUpdate.expectedRevision == 3,
            "update revision forwarded");
    require(lifecycle.lastUpdate.draft.customerId == 45,
            "updated draft forwarded");
    require(lifecycle.lastUpdate.actorUserId == 1,
            "update actor comes from session");

    response = sendJson(
        network, port, QByteArrayLiteral("PATCH"),
        QStringLiteral("/api/v1/quotes/7/status"),
        QJsonObject{
            {QStringLiteral("status"), QStringLiteral("issued")},
            {QStringLiteral("revision"), 4},
        },
        token
    );
    require(response.status == 200, "quote issue returns 200");
    require(lifecycle.lastStatus.id == 7, "status quote id forwarded");
    require(lifecycle.lastStatus.expectedRevision == 4,
            "status revision forwarded");
    require(lifecycle.lastStatus.targetStatus == manage::data::QuoteStatus::Issued,
            "status target forwarded");
    require(lifecycle.lastStatus.actorUserId == 1,
            "status actor comes from session");
    object = responseObject(response);
    require(object.value(QStringLiteral("status")).toString() ==
                QStringLiteral("issued"),
            "issued JSON returned");
    require(object.value(QStringLiteral("issuedAt")).isString(),
            "issuedAt timestamp returned");

    response = sendJson(
        network, port, QByteArrayLiteral("PATCH"),
        QStringLiteral("/api/v1/quotes/7/status"),
        QJsonObject{
            {QStringLiteral("status"), QStringLiteral("void")},
            {QStringLiteral("revision"), 5},
        },
        token
    );
    require(response.status == 200, "quote void returns 200");
    require(lifecycle.lastStatus.targetStatus == manage::data::QuoteStatus::Void,
            "void status target forwarded");
    object = responseObject(response);
    require(object.value(QStringLiteral("status")).toString() ==
                QStringLiteral("void"),
            "void JSON returned");
    require(object.value(QStringLiteral("voidedAt")).isString(),
            "voidedAt timestamp returned");

    response = sendJson(
        network, port, QByteArrayLiteral("POST"),
        QStringLiteral("/api/v1/quotes/7/clone"), QJsonObject{}, token
    );
    require(response.status == 201, "quote clone returns 201");
    require(lifecycle.lastClone.sourceId == 7, "clone source id forwarded");
    require(lifecycle.lastClone.actorUserId == 1,
            "clone actor comes from session");
    object = responseObject(response);
    require(object.value(QStringLiteral("sourceQuoteId")).toInteger() == 7,
            "clone sourceQuoteId JSON");
    require(object.value(QStringLiteral("status")).toString() ==
                QStringLiteral("draft"),
            "clone returns new draft");

    response = waitForReply(network.deleteResource(request(
        port, QStringLiteral("/api/v1/quotes/8?revision=1"), token
    )));
    require(response.status == 204, "draft delete returns 204");
    require(response.body.isEmpty(), "draft delete response has no body");
    require(lifecycle.lastDelete.id == 8, "delete quote id forwarded");
    require(lifecycle.lastDelete.expectedRevision == 1,
            "delete revision forwarded");
    require(lifecycle.lastDelete.actorUserId == 1,
            "delete actor comes from session");
}

void checkValidationAndErrors(
    QNetworkAccessManager& network,
    quint16 port,
    const QByteArray& token,
    FakeQuoteLifecycle& lifecycle
) {
    auto response = waitForReply(network.get(request(
        port, QStringLiteral("/api/v1/quotes?status=bad"), token
    )));
    require(response.status == 400, "invalid status filter returns 400");
    require(responseError(response) == QStringLiteral("invalid_request"),
            "invalid status error code");

    response = sendJson(
        network, port, QByteArrayLiteral("POST"),
        QStringLiteral("/api/v1/quotes"), QJsonObject{}, token
    );
    require(response.status == 400, "incomplete quote create returns 400");

    response = waitForReply(network.sendCustomRequest(
        request(port, QStringLiteral("/api/v1/quotes"), token),
        QByteArrayLiteral("POST"),
        QByteArrayLiteral("{")
    ));
    require(response.status == 400, "malformed quote JSON returns 400");
    require(responseError(response) == QStringLiteral("invalid_json"),
            "malformed quote JSON error code");

    response = sendJson(
        network, port, QByteArrayLiteral("PATCH"),
        QStringLiteral("/api/v1/quotes/7/status"),
        QJsonObject{
            {QStringLiteral("status"), QStringLiteral("draft")},
            {QStringLiteral("revision"), 1},
        }, token
    );
    require(response.status == 400, "status endpoint rejects draft target");

    response = waitForReply(network.deleteResource(request(
        port, QStringLiteral("/api/v1/quotes/7"), token
    )));
    require(response.status == 400, "delete requires revision query");

    struct Mapping final {
        manage::data::QuoteErrorCode code;
        int status;
        QString error;
    };
    const Mapping mappings[]{
        {manage::data::QuoteErrorCode::Validation, 400,
         QStringLiteral("invalid_request")},
        {manage::data::QuoteErrorCode::NotFound, 404,
         QStringLiteral("not_found")},
        {manage::data::QuoteErrorCode::Conflict, 409,
         QStringLiteral("revision_conflict")},
        {manage::data::QuoteErrorCode::Duplicate, 409,
         QStringLiteral("duplicate_quote")},
        {manage::data::QuoteErrorCode::InvalidTransition, 409,
         QStringLiteral("invalid_transition")},
        {manage::data::QuoteErrorCode::Infrastructure, 500,
         QStringLiteral("database_error")},
    };
    for (const auto& mapping : mappings) {
        lifecycle.failNext(mapping.code);
        response = waitForReply(network.get(request(
            port, QStringLiteral("/api/v1/quotes/999"), token
        )));
        require(response.status == mapping.status,
                "QuoteErrorCode HTTP status mapping");
        require(responseError(response) == mapping.error,
                "QuoteErrorCode structured error mapping");
    }
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    auto users = std::make_shared<manage::tests::FakeUserRepository>();
    auto authService = std::make_shared<manage::auth::AuthService>(
        users,
        manage::auth::PasswordHasher(
            manage::auth::PasswordHasher::kMinimumIterations
        )
    );
    require(
        authService->bootstrapAdministrator(
            QStringLiteral("Correct Horse Battery 1"),
            QStringLiteral("Quote administrator")
        ).succeeded(),
        "quote administrator bootstrap"
    );
    const auto temporaryLogin = authService->login(
        QStringLiteral("admin"), QStringLiteral("Correct Horse Battery 1")
    );
    require(temporaryLogin.succeeded(), "temporary quote administrator login");

    FakeQuoteLifecycle lifecycle;
    manage::server::ApiServer server(authService, nullptr, nullptr, &lifecycle);
    const auto port = server.listen(QHostAddress::LocalHost, 0);
    if (port == 0) {
        std::cerr << "[FAIL] unable to start quote route test server\n";
        return EXIT_FAILURE;
    }

    QNetworkAccessManager network;
    try {
        checkAuthorization(
            network,
            port,
            temporaryLogin.session.accessToken.toLatin1(),
            authService,
            users,
            lifecycle
        );
        std::cout << "[PASS] quote authorization rules\n";

        const auto activeLogin = authService->login(
            QStringLiteral("admin"), QStringLiteral("Replaced Horse Battery 2")
        );
        require(activeLogin.succeeded(), "active quote administrator login");
        const auto token = activeLogin.session.accessToken.toLatin1();
        checkReadAndWriteRoutes(network, port, token, lifecycle);
        std::cout << "[PASS] quote REST lifecycle and JSON contract\n";
        checkValidationAndErrors(network, port, token, lifecycle);
        std::cout << "[PASS] quote validation and error mapping\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] quote routes: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
