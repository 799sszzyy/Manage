#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>

namespace {

class WorkflowFailure final : public std::runtime_error {
public:
    explicit WorkflowFailure(const QString& message)
        : std::runtime_error(message.toUtf8().constData()) {}
};

void require(bool condition, const QString& message) {
    if (!condition) {
        throw WorkflowFailure(message);
    }
}

struct Response final {
    int status{};
    QByteArray body;
    QString networkError;

    QJsonObject object() const {
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(body, &error);
        require(
            error.error == QJsonParseError::NoError && document.isObject(),
            QStringLiteral("response is not a JSON object: %1")
                .arg(QString::fromUtf8(body.left(500)))
        );
        return document.object();
    }
};

struct WorkflowConfig final {
    QUrl baseUrl;
    QString serverExecutable;
    QString prefix;
    QString temporaryPassword;
    QString permanentPassword;
    bool strictAuthorization{true};
    bool allowExternalNoRestart{false};
    int timeoutMs{10'000};
};

QString uniquePrefix() {
    return QStringLiteral("E2E-%1-%2")
        .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddHHmmsszzz")))
        .arg(QRandomGenerator::system()->generate(), 8, 16, QLatin1Char('0'))
        .toUpper();
}

QString safetyError(
    const QString& enabled,
    const QString& databaseName,
    const QUrl& baseUrl,
    const QString& prefix
) {
    if (enabled != QStringLiteral("1")) {
        return QStringLiteral("MANAGE_E2E=1 is required for a real E2E run");
    }
    if (!databaseName.endsWith(QStringLiteral("_test"), Qt::CaseInsensitive)) {
        return QStringLiteral("MANAGE_DB_NAME must end with _test");
    }
    if (!baseUrl.isValid() || baseUrl.scheme() != QStringLiteral("http") ||
        (baseUrl.host() != QStringLiteral("127.0.0.1") &&
         baseUrl.host().compare(QStringLiteral("localhost"), Qt::CaseInsensitive) != 0) ||
        baseUrl.port() <= 0 ||
        (!baseUrl.path().isEmpty() && baseUrl.path() != QStringLiteral("/"))) {
        return QStringLiteral(
            "base URL must be http://127.0.0.1:<port> or http://localhost:<port>"
        );
    }
    static const QRegularExpression prefixPattern(
        QStringLiteral(R"(^E2E-[A-Z0-9-]{8,40}$)")
    );
    if (!prefixPattern.match(prefix).hasMatch()) {
        return QStringLiteral(
            "test prefix must start with E2E- and contain only uppercase letters, digits and hyphens"
        );
    }
    return {};
}

class HttpClient final {
public:
    HttpClient(QUrl baseUrl, int timeoutMs)
        : baseUrl_(std::move(baseUrl)), timeoutMs_(timeoutMs) {
        auto path = baseUrl_.path();
        if (path == QStringLiteral("/")) {
            baseUrl_.setPath({});
        }
    }

    Response get(const QString& path, const QByteArray& token = {}) {
        return send(QByteArrayLiteral("GET"), path, std::nullopt, token);
    }

    Response sendJson(
        const QByteArray& method,
        const QString& path,
        const QJsonObject& body,
        const QByteArray& token = {}
    ) {
        return send(method, path, body, token);
    }

private:
    Response send(
        const QByteArray& method,
        const QString& path,
        const std::optional<QJsonObject>& body,
        const QByteArray& token
    ) {
        auto url = baseUrl_;
        const QUrl relative(path);
        url.setPath(relative.path());
        url.setQuery(relative.query());
        QNetworkRequest request(url);
        request.setHeader(
            QNetworkRequest::ContentTypeHeader,
            QStringLiteral("application/json")
        );
        if (!token.isEmpty()) {
            request.setRawHeader(QByteArrayLiteral("Authorization"),
                                 QByteArrayLiteral("Bearer ") + token);
        }

        QNetworkReply* reply = nullptr;
        const auto bytes = body.has_value()
                               ? QJsonDocument(*body).toJson(QJsonDocument::Compact)
                               : QByteArray{};
        if (method == QByteArrayLiteral("GET")) {
            reply = network_.get(request);
        } else if (method == QByteArrayLiteral("POST")) {
            reply = network_.post(request, bytes);
        } else if (method == QByteArrayLiteral("PUT")) {
            reply = network_.put(request, bytes);
        } else {
            reply = network_.sendCustomRequest(request, method, bytes);
        }

        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QObject::connect(&timeout, &QTimer::timeout, &loop, [&]() {
            reply->abort();
            loop.quit();
        });
        timeout.start(timeoutMs_);
        if (!reply->isFinished()) {
            loop.exec();
        }

        Response response;
        response.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                              .toInt();
        response.body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            response.networkError = reply->errorString();
        }
        reply->deleteLater();
        return response;
    }

    QUrl baseUrl_;
    int timeoutMs_;
    QNetworkAccessManager network_;
};

QString responseSummary(const Response& response) {
    return QStringLiteral("HTTP %1; body=%2; network=%3")
        .arg(response.status)
        .arg(QString::fromUtf8(response.body.left(500)))
        .arg(response.networkError);
}

QJsonObject expectStatus(
    const Response& response,
    int status,
    const QString& operation
) {
    require(
        response.status == status,
        QStringLiteral("%1 expected HTTP %2, got %3")
            .arg(operation)
            .arg(status)
            .arg(responseSummary(response))
    );
    return response.object();
}

void expectEmptyStatus(
    const Response& response,
    int status,
    const QString& operation
) {
    require(
        response.status == status,
        QStringLiteral("%1 expected HTTP %2, got %3")
            .arg(operation)
            .arg(status)
            .arg(responseSummary(response))
    );
    require(
        response.body.trimmed().isEmpty(),
        operation + QStringLiteral(" expected an empty response body")
    );
}

qint64 positiveId(const QJsonObject& object, const QString& operation) {
    const auto id = object.value(QStringLiteral("id")).toInteger();
    require(id > 0, operation + QStringLiteral(" did not return a positive id"));
    return id;
}

int positiveRevision(const QJsonObject& object, const QString& operation) {
    const auto revision = object.value(QStringLiteral("revision")).toInt();
    require(revision > 0, operation + QStringLiteral(" did not return a revision"));
    return revision;
}

class ManagedServer final {
public:
    ManagedServer(QString executable, QUrl baseUrl, int timeoutMs)
        : executable_(std::move(executable)),
          baseUrl_(std::move(baseUrl)),
          timeoutMs_(timeoutMs) {}

    ManagedServer(const ManagedServer&) = delete;
    ManagedServer& operator=(const ManagedServer&) = delete;

    ~ManagedServer() { stop(); }

    void migrate() {
        QProcess migration;
        migration.setProgram(executable_);
        migration.setArguments({
            QStringLiteral("--port"), QString::number(baseUrl_.port()),
            QStringLiteral("--migrate-only"),
        });
        migration.start();
        require(migration.waitForStarted(timeoutMs_),
                QStringLiteral("could not start migration process"));
        require(migration.waitForFinished(60'000),
                QStringLiteral("migration process timed out"));
        require(
            migration.exitStatus() == QProcess::NormalExit && migration.exitCode() == 0,
            QStringLiteral("migration failed: %1 %2")
                .arg(QString::fromUtf8(migration.readAllStandardOutput()))
                .arg(QString::fromUtf8(migration.readAllStandardError()))
        );
        std::cout << "[PASS] database migrations completed\n";
    }

    void start(HttpClient& client) {
        require(process_.state() == QProcess::NotRunning,
                QStringLiteral("server process is already running"));
        process_.setProgram(executable_);
        process_.setArguments({
            QStringLiteral("--port"), QString::number(baseUrl_.port()),
        });
        process_.start();
        require(process_.waitForStarted(timeoutMs_),
                QStringLiteral("could not start manage-server"));

        const auto deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs_;
        while (QDateTime::currentMSecsSinceEpoch() < deadline) {
            if (process_.state() == QProcess::NotRunning) {
                throw WorkflowFailure(QStringLiteral("manage-server exited early: %1")
                    .arg(QString::fromUtf8(process_.readAllStandardError())));
            }
            const auto response = client.get(QStringLiteral("/api/v1/health"));
            if (response.status == 200) {
                std::cout << "[PASS] manage-server is ready\n";
                return;
            }
            QThread::msleep(100);
        }
        throw WorkflowFailure(QStringLiteral("manage-server health check timed out"));
    }

    void stop() {
        if (process_.state() == QProcess::NotRunning) {
            return;
        }
        process_.terminate();
        if (!process_.waitForFinished(5'000)) {
            process_.kill();
            process_.waitForFinished(5'000);
        }
    }

    void restart(HttpClient& client) {
        stop();
        start(client);
        std::cout << "[PASS] manage-server restarted\n";
    }

private:
    QString executable_;
    QUrl baseUrl_;
    int timeoutMs_;
    QProcess process_;
};

struct CreatedRecords final {
    qint64 customerId{};
    qint64 materialId{};
    qint64 bomId{};
    qint64 quoteId{};
    int quoteRevision{};
    QString quoteNumber;
    QString customerSnapshot;
    QString materialSnapshot;
    qint64 quotePriceWithTaxCents{};
};

void expectAuthError(
    const Response& response,
    int status,
    const QString& code,
    const QString& operation
) {
    const auto object = expectStatus(response, status, operation);
    require(
        object.value(QStringLiteral("error")).toString() == code,
        QStringLiteral("%1 expected error '%2', got '%3'")
            .arg(operation, code, object.value(QStringLiteral("error")).toString())
    );
}

QByteArray login(HttpClient& client, const QString& password, bool mustChange) {
    const auto response = client.sendJson(
        QByteArrayLiteral("POST"),
        QStringLiteral("/api/v1/auth/login"),
        QJsonObject{
            {QStringLiteral("username"), QStringLiteral("admin")},
            {QStringLiteral("password"), password},
        }
    );
    const auto object = expectStatus(response, 200, QStringLiteral("admin login"));
    const auto token = object.value(QStringLiteral("accessToken")).toString().toLatin1();
    require(!token.isEmpty(), QStringLiteral("admin login returned no access token"));
    require(
        object.value(QStringLiteral("user")).toObject()
                .value(QStringLiteral("mustChangePassword")).toBool() == mustChange,
        QStringLiteral("admin login returned unexpected mustChangePassword state")
    );
    return token;
}

void checkStrictAuthorizationBeforePasswordChange(
    HttpClient& client,
    const QByteArray& temporaryToken
) {
    expectAuthError(
        client.get(QStringLiteral("/api/v1/materials")),
        401,
        QStringLiteral("unauthorized"),
        QStringLiteral("anonymous catalog read")
    );
    expectAuthError(
        client.get(QStringLiteral("/api/v1/materials"), temporaryToken),
        403,
        QStringLiteral("password_change_required"),
        QStringLiteral("temporary-password catalog read")
    );
    expectAuthError(
        client.get(QStringLiteral("/api/v1/quotes")),
        401,
        QStringLiteral("unauthorized"),
        QStringLiteral("anonymous quote read")
    );
    expectAuthError(
        client.get(QStringLiteral("/api/v1/quotes"), temporaryToken),
        403,
        QStringLiteral("password_change_required"),
        QStringLiteral("temporary-password quote read")
    );
    std::cout << "[PASS] strict authorization rejects anonymous and temporary sessions\n";
}

CreatedRecords createAndModifyBusinessData(
    HttpClient& client,
    const WorkflowConfig& config,
    const QByteArray& token
) {
    const auto customerName = config.prefix + QStringLiteral("-CUSTOMER");
    auto customer = expectStatus(
        client.sendJson(
            QByteArrayLiteral("POST"),
            QStringLiteral("/api/v1/customers"),
            QJsonObject{{QStringLiteral("name"), customerName}},
            token
        ),
        201,
        QStringLiteral("customer creation")
    );
    const auto customerId = positiveId(customer, QStringLiteral("customer creation"));
    auto customerRevision = positiveRevision(customer, QStringLiteral("customer creation"));
    customer = expectStatus(
        client.sendJson(
            QByteArrayLiteral("PUT"),
            QStringLiteral("/api/v1/customers/%1").arg(customerId),
            QJsonObject{
                {QStringLiteral("name"), customerName},
                {QStringLiteral("contactName"), config.prefix + QStringLiteral("-CONTACT")},
                {QStringLiteral("phone"), QStringLiteral("E2E-ONLY")},
                {QStringLiteral("revision"), customerRevision},
            },
            token
        ),
        200,
        QStringLiteral("customer update")
    );
    require(positiveRevision(customer, QStringLiteral("customer update")) ==
                customerRevision + 1,
            QStringLiteral("customer update did not increment revision"));

    const auto materialCode = config.prefix + QStringLiteral("-MAT");
    auto material = expectStatus(
        client.sendJson(
            QByteArrayLiteral("POST"),
            QStringLiteral("/api/v1/materials"),
            QJsonObject{
                {QStringLiteral("code"), materialCode},
                {QStringLiteral("name"), config.prefix + QStringLiteral("-MATERIAL")},
                {QStringLiteral("unit"), QStringLiteral("piece")},
                {QStringLiteral("currentUnitPriceCents"), 12345},
            },
            token
        ),
        201,
        QStringLiteral("material creation")
    );
    const auto materialId = positiveId(material, QStringLiteral("material creation"));
    auto materialRevision = positiveRevision(material, QStringLiteral("material creation"));
    material = expectStatus(
        client.sendJson(
            QByteArrayLiteral("PUT"),
            QStringLiteral("/api/v1/materials/%1").arg(materialId),
            QJsonObject{
                {QStringLiteral("code"), materialCode},
                {QStringLiteral("name"), config.prefix + QStringLiteral("-MATERIAL-UPDATED")},
                {QStringLiteral("specification"), QStringLiteral("E2E")},
                {QStringLiteral("unit"), QStringLiteral("piece")},
                {QStringLiteral("category"), QStringLiteral("TEST")},
                {QStringLiteral("currentUnitPriceCents"), 23456},
                {QStringLiteral("revision"), materialRevision},
            },
            token
        ),
        200,
        QStringLiteral("material update")
    );
    materialRevision = positiveRevision(material, QStringLiteral("material update"));

    const auto bomCode = config.prefix + QStringLiteral("-BOM");
    auto bom = expectStatus(
        client.sendJson(
            QByteArrayLiteral("POST"),
            QStringLiteral("/api/v1/boms"),
            QJsonObject{
                {QStringLiteral("code"), bomCode},
                {QStringLiteral("name"), config.prefix + QStringLiteral("-ASSEMBLY")},
                {QStringLiteral("items"), QJsonArray{
                    QJsonObject{
                        {QStringLiteral("lineNo"), 10},
                        {QStringLiteral("materialId"), materialId},
                        {QStringLiteral("quantityMicros"), 2'000'000},
                    },
                }},
            },
            token
        ),
        201,
        QStringLiteral("BOM creation")
    );
    const auto bomId = positiveId(bom, QStringLiteral("BOM creation"));
    auto bomRevision = positiveRevision(bom, QStringLiteral("BOM creation"));
    bom = expectStatus(
        client.sendJson(
            QByteArrayLiteral("PUT"),
            QStringLiteral("/api/v1/boms/%1").arg(bomId),
            QJsonObject{
                {QStringLiteral("code"), bomCode},
                {QStringLiteral("name"), config.prefix + QStringLiteral("-ASSEMBLY-UPDATED")},
                {QStringLiteral("description"), QStringLiteral("E2E only")},
                {QStringLiteral("revision"), bomRevision},
            },
            token
        ),
        200,
        QStringLiteral("BOM update")
    );
    bomRevision = positiveRevision(bom, QStringLiteral("BOM update"));
    bom = expectStatus(
        client.sendJson(
            QByteArrayLiteral("PUT"),
            QStringLiteral("/api/v1/boms/%1/items").arg(bomId),
            QJsonObject{
                {QStringLiteral("revision"), bomRevision},
                {QStringLiteral("items"), QJsonArray{
                    QJsonObject{
                        {QStringLiteral("lineNo"), 20},
                        {QStringLiteral("materialId"), materialId},
                        {QStringLiteral("quantityMicros"), 3'000'000},
                        {QStringLiteral("notes"), config.prefix},
                    },
                }},
            },
            token
        ),
        200,
        QStringLiteral("BOM item replacement")
    );
    bomRevision = positiveRevision(bom, QStringLiteral("BOM item replacement"));
    bom = expectStatus(
        client.sendJson(
            QByteArrayLiteral("PATCH"),
            QStringLiteral("/api/v1/boms/%1/enabled").arg(bomId),
            QJsonObject{
                {QStringLiteral("revision"), bomRevision},
                {QStringLiteral("isEnabled"), false},
            },
            token
        ),
        200,
        QStringLiteral("BOM disable")
    );
    require(!bom.value(QStringLiteral("isEnabled")).toBool(true),
            QStringLiteral("BOM was not disabled"));

    material = expectStatus(
        client.sendJson(
            QByteArrayLiteral("PATCH"),
            QStringLiteral("/api/v1/materials/%1/enabled").arg(materialId),
            QJsonObject{
                {QStringLiteral("revision"), materialRevision},
                {QStringLiteral("isEnabled"), false},
            },
            token
        ),
        200,
        QStringLiteral("material disable")
    );
    require(!material.value(QStringLiteral("isEnabled")).toBool(true),
            QStringLiteral("material was not disabled"));

    std::cout << "[PASS] customer, material and BOM lifecycle completed\n";
    return {customerId, materialId, bomId};
}

QJsonObject quotePayload(
    const CreatedRecords& records,
    const WorkflowConfig& config,
    const QString& notes,
    qint64 unitPriceCents = 23'456
) {
    return {
        {QStringLiteral("customerId"), records.customerId},
        {QStringLiteral("bomTemplateId"), records.bomId},
        {QStringLiteral("freightCents"), 1'000},
        {QStringLiteral("otherFeesCents"), 200},
        {QStringLiteral("markupBasisPoints"), 2'000},
        {QStringLiteral("taxBasisPoints"), 1'300},
        {QStringLiteral("notes"), notes},
        {QStringLiteral("items"), QJsonArray{
            QJsonObject{
                {QStringLiteral("materialId"), records.materialId},
                {QStringLiteral("quantityMicros"), 2'000'000},
                {QStringLiteral("unitPriceCents"), unitPriceCents},
                {QStringLiteral("notes"), config.prefix + QStringLiteral("-LINE")},
            },
        }},
    };
}

void createQuoteDraft(
    HttpClient& client,
    const WorkflowConfig& config,
    const QByteArray& token,
    CreatedRecords& records
) {
    auto payload = quotePayload(
        records, config, config.prefix + QStringLiteral("-QUOTE-DRAFT")
    );
    payload.insert(QStringLiteral("customerName"), QStringLiteral("FORGED-CUSTOMER"));
    payload.insert(QStringLiteral("priceWithTaxCents"), 1);
    payload.insert(QStringLiteral("materialCostCents"), 1);

    const auto quote = expectStatus(
        client.sendJson(
            QByteArrayLiteral("POST"),
            QStringLiteral("/api/v1/quotes"),
            payload,
            token
        ),
        201,
        QStringLiteral("quote draft creation")
    );
    records.quoteId = positiveId(quote, QStringLiteral("quote draft creation"));
    records.quoteRevision = positiveRevision(
        quote, QStringLiteral("quote draft creation")
    );
    records.quoteNumber = quote.value(QStringLiteral("quoteNumber")).toString();
    records.customerSnapshot = quote.value(QStringLiteral("customerName")).toString();
    records.quotePriceWithTaxCents =
        quote.value(QStringLiteral("priceWithTaxCents")).toInteger();

    const auto items = quote.value(QStringLiteral("items")).toArray();
    require(items.size() == 1, QStringLiteral("saved quote must contain one item"));
    const auto item = items.first().toObject();
    records.materialSnapshot = item.value(QStringLiteral("materialName")).toString();
    require(quote.value(QStringLiteral("status")).toString() == QStringLiteral("draft"),
            QStringLiteral("new quote must be a draft"));
    require(records.quoteNumber.startsWith(QStringLiteral("Q-")),
            QStringLiteral("server did not generate a quote number"));
    require(records.customerSnapshot.startsWith(config.prefix) &&
                records.customerSnapshot != QStringLiteral("FORGED-CUSTOMER"),
            QStringLiteral("customer snapshot was not loaded from MySQL"));
    require(records.materialSnapshot.startsWith(config.prefix),
            QStringLiteral("material snapshot was not loaded from MySQL"));
    require(item.value(QStringLiteral("subtotalCents")).toInteger() == 46'912,
            QStringLiteral("quote line subtotal was not calculated by the server"));
    require(quote.value(QStringLiteral("materialCostCents")).toInteger() == 46'912 &&
                records.quotePriceWithTaxCents > 46'912,
            QStringLiteral("quote totals were not calculated by the domain module"));

    const auto page = expectStatus(
        client.get(
            QStringLiteral("/api/v1/quotes?status=draft&search=%1")
                .arg(records.quoteNumber),
            token
        ),
        200,
        QStringLiteral("quote list search")
    );
    require(page.value(QStringLiteral("total")).toInteger() == 1 &&
                page.value(QStringLiteral("items")).toArray().size() == 1,
            QStringLiteral("saved quote was not returned by filtered list"));
    std::cout << "[PASS] quote draft, snapshots, totals and filtered list completed\n";
}

void expectQuoteError(
    const Response& response,
    const QString& expectedCode,
    const QString& operation
) {
    const auto object = expectStatus(response, 409, operation);
    require(object.value(QStringLiteral("error")).toString() == expectedCode,
            QStringLiteral("%1 returned unexpected quote error '%2'")
                .arg(operation, object.value(QStringLiteral("error")).toString()));
}

void completeQuoteLifecycleAfterRestart(
    HttpClient& client,
    const WorkflowConfig& config,
    const CreatedRecords& records,
    const QByteArray& token
) {
    auto quote = expectStatus(
        client.get(
            QStringLiteral("/api/v1/quotes/%1").arg(records.quoteId), token
        ),
        200,
        QStringLiteral("persisted quote read")
    );
    require(quote.value(QStringLiteral("revision")).toInt() == records.quoteRevision &&
                quote.value(QStringLiteral("priceWithTaxCents")).toInteger() ==
                    records.quotePriceWithTaxCents,
            QStringLiteral("quote did not persist across server restart"));

    auto updatedPayload = quotePayload(
        records,
        config,
        config.prefix + QStringLiteral("-QUOTE-UPDATED"),
        25'000
    );
    updatedPayload.insert(QStringLiteral("revision"), records.quoteRevision);
    quote = expectStatus(
        client.sendJson(
            QByteArrayLiteral("PUT"),
            QStringLiteral("/api/v1/quotes/%1").arg(records.quoteId),
            updatedPayload,
            token
        ),
        200,
        QStringLiteral("draft quote update")
    );
    const auto updatedRevision = positiveRevision(
        quote, QStringLiteral("draft quote update")
    );
    require(updatedRevision == records.quoteRevision + 1,
            QStringLiteral("draft update did not increment revision"));

    auto stalePayload = quotePayload(
        records, config, config.prefix + QStringLiteral("-STALE"), 26'000
    );
    stalePayload.insert(QStringLiteral("revision"), records.quoteRevision);
    expectQuoteError(
        client.sendJson(
            QByteArrayLiteral("PUT"),
            QStringLiteral("/api/v1/quotes/%1").arg(records.quoteId),
            stalePayload,
            token
        ),
        QStringLiteral("revision_conflict"),
        QStringLiteral("stale draft update")
    );

    const auto issued = expectStatus(
        client.sendJson(
            QByteArrayLiteral("PATCH"),
            QStringLiteral("/api/v1/quotes/%1/status").arg(records.quoteId),
            QJsonObject{
                {QStringLiteral("status"), QStringLiteral("issued")},
                {QStringLiteral("revision"), updatedRevision},
            },
            token
        ),
        200,
        QStringLiteral("quote issue")
    );
    const auto issuedRevision = positiveRevision(issued, QStringLiteral("quote issue"));
    require(issued.value(QStringLiteral("status")).toString() == QStringLiteral("issued") &&
                issued.value(QStringLiteral("issuedAt")).isString(),
            QStringLiteral("issued quote did not freeze with a timestamp"));

    auto frozenUpdate = quotePayload(
        records, config, config.prefix + QStringLiteral("-FROZEN"), 27'000
    );
    frozenUpdate.insert(QStringLiteral("revision"), issuedRevision);
    expectQuoteError(
        client.sendJson(
            QByteArrayLiteral("PUT"),
            QStringLiteral("/api/v1/quotes/%1").arg(records.quoteId),
            frozenUpdate,
            token
        ),
        QStringLiteral("revision_conflict"),
        QStringLiteral("issued quote update")
    );

    const auto frozenCustomer = issued.value(QStringLiteral("customerName")).toString();
    const auto frozenItems = issued.value(QStringLiteral("items")).toArray();
    require(frozenItems.size() == 1, QStringLiteral("issued quote lost its line item"));
    const auto frozenMaterial = frozenItems.first().toObject()
                                    .value(QStringLiteral("materialName"))
                                    .toString();
    const auto frozenUnitPrice = frozenItems.first().toObject()
                                     .value(QStringLiteral("unitPriceCents"))
                                     .toInteger();
    const auto frozenTotal = issued.value(QStringLiteral("priceWithTaxCents")).toInteger();

    auto customer = expectStatus(
        client.get(
            QStringLiteral("/api/v1/customers/%1").arg(records.customerId), token
        ),
        200,
        QStringLiteral("quote source customer read")
    );
    expectStatus(
        client.sendJson(
            QByteArrayLiteral("PUT"),
            QStringLiteral("/api/v1/customers/%1").arg(records.customerId),
            QJsonObject{
                {QStringLiteral("name"), config.prefix + QStringLiteral("-CUSTOMER-CHANGED")},
                {QStringLiteral("contactName"), customer.value(QStringLiteral("contactName"))},
                {QStringLiteral("phone"), customer.value(QStringLiteral("phone"))},
                {QStringLiteral("address"), customer.value(QStringLiteral("address"))},
                {QStringLiteral("revision"), customer.value(QStringLiteral("revision"))},
            },
            token
        ),
        200,
        QStringLiteral("quote source customer update")
    );

    auto material = expectStatus(
        client.get(
            QStringLiteral("/api/v1/materials/%1").arg(records.materialId), token
        ),
        200,
        QStringLiteral("quote source material read")
    );
    expectStatus(
        client.sendJson(
            QByteArrayLiteral("PUT"),
            QStringLiteral("/api/v1/materials/%1").arg(records.materialId),
            QJsonObject{
                {QStringLiteral("code"), material.value(QStringLiteral("code"))},
                {QStringLiteral("name"), config.prefix + QStringLiteral("-MATERIAL-CHANGED")},
                {QStringLiteral("specification"), material.value(QStringLiteral("specification"))},
                {QStringLiteral("unit"), material.value(QStringLiteral("unit"))},
                {QStringLiteral("category"), material.value(QStringLiteral("category"))},
                {QStringLiteral("currentUnitPriceCents"), 99'999},
                {QStringLiteral("revision"), material.value(QStringLiteral("revision"))},
            },
            token
        ),
        200,
        QStringLiteral("quote source material update")
    );

    const auto frozen = expectStatus(
        client.get(
            QStringLiteral("/api/v1/quotes/%1").arg(records.quoteId), token
        ),
        200,
        QStringLiteral("issued quote snapshot reread")
    );
    const auto rereadItem = frozen.value(QStringLiteral("items"))
                                .toArray()
                                .first()
                                .toObject();
    require(frozen.value(QStringLiteral("customerName")).toString() == frozenCustomer &&
                rereadItem.value(QStringLiteral("materialName")).toString() ==
                    frozenMaterial &&
                rereadItem.value(QStringLiteral("unitPriceCents")).toInteger() ==
                    frozenUnitPrice &&
                frozen.value(QStringLiteral("priceWithTaxCents")).toInteger() == frozenTotal,
            QStringLiteral("issued quote snapshots changed with source records"));

    const auto cloned = expectStatus(
        client.sendJson(
            QByteArrayLiteral("POST"),
            QStringLiteral("/api/v1/quotes/%1/clone").arg(records.quoteId),
            QJsonObject{},
            token
        ),
        201,
        QStringLiteral("issued quote clone")
    );
    require(cloned.value(QStringLiteral("status")).toString() == QStringLiteral("draft") &&
                cloned.value(QStringLiteral("sourceQuoteId")).toInteger() == records.quoteId &&
                positiveId(cloned, QStringLiteral("issued quote clone")) != records.quoteId &&
                cloned.value(QStringLiteral("quoteNumber")).toString() != records.quoteNumber,
            QStringLiteral("quote clone did not create an independent draft"));

    expectQuoteError(
        client.sendJson(
            QByteArrayLiteral("DELETE"),
            QStringLiteral("/api/v1/quotes/%1?revision=%2")
                .arg(records.quoteId)
                .arg(issuedRevision),
            QJsonObject{},
            token
        ),
        QStringLiteral("revision_conflict"),
        QStringLiteral("issued quote delete")
    );

    const auto voided = expectStatus(
        client.sendJson(
            QByteArrayLiteral("PATCH"),
            QStringLiteral("/api/v1/quotes/%1/status").arg(records.quoteId),
            QJsonObject{
                {QStringLiteral("status"), QStringLiteral("void")},
                {QStringLiteral("revision"), issuedRevision},
            },
            token
        ),
        200,
        QStringLiteral("quote void")
    );
    require(voided.value(QStringLiteral("status")).toString() == QStringLiteral("void") &&
                voided.value(QStringLiteral("voidedAt")).isString(),
            QStringLiteral("void quote did not record its terminal state"));

    const auto disposable = expectStatus(
        client.sendJson(
            QByteArrayLiteral("POST"),
            QStringLiteral("/api/v1/quotes"),
            quotePayload(
                records, config, config.prefix + QStringLiteral("-DELETE-ME")
            ),
            token
        ),
        201,
        QStringLiteral("disposable draft creation")
    );
    const auto disposableId = positiveId(
        disposable, QStringLiteral("disposable draft creation")
    );
    const auto disposableRevision = positiveRevision(
        disposable, QStringLiteral("disposable draft creation")
    );
    expectEmptyStatus(
        client.sendJson(
            QByteArrayLiteral("DELETE"),
            QStringLiteral("/api/v1/quotes/%1?revision=%2")
                .arg(disposableId)
                .arg(disposableRevision),
            QJsonObject{},
            token
        ),
        204,
        QStringLiteral("draft quote delete")
    );
    const auto missing = expectStatus(
        client.get(
            QStringLiteral("/api/v1/quotes/%1").arg(disposableId), token
        ),
        404,
        QStringLiteral("deleted quote read")
    );
    require(missing.value(QStringLiteral("error")).toString() ==
                QStringLiteral("not_found"),
            QStringLiteral("deleted quote did not disappear"));

    const auto voidPage = expectStatus(
        client.get(
            QStringLiteral("/api/v1/quotes?status=void&search=%1")
                .arg(records.quoteNumber),
            token
        ),
        200,
        QStringLiteral("void quote list search")
    );
    require(voidPage.value(QStringLiteral("total")).toInteger() == 1,
            QStringLiteral("void quote was not returned by filtered list"));
    std::cout << "[PASS] quote restart, update, conflict, issue, snapshots, clone, void and delete completed\n";
}

void verifyPersistedData(
    HttpClient& client,
    const WorkflowConfig& config,
    const CreatedRecords& records,
    const QByteArray& token
) {
    const auto customer = expectStatus(
        client.get(QStringLiteral("/api/v1/customers/%1").arg(records.customerId), token),
        200,
        QStringLiteral("persisted customer read")
    );
    require(customer.value(QStringLiteral("name")).toString().startsWith(config.prefix),
            QStringLiteral("persisted customer does not belong to this E2E prefix"));

    const auto material = expectStatus(
        client.get(QStringLiteral("/api/v1/materials/%1").arg(records.materialId), token),
        200,
        QStringLiteral("persisted material read")
    );
    require(material.value(QStringLiteral("code")).toString().startsWith(config.prefix),
            QStringLiteral("persisted material does not belong to this E2E prefix"));
    require(!material.value(QStringLiteral("isEnabled")).toBool(true),
            QStringLiteral("persisted material disable state was lost"));

    const auto bom = expectStatus(
        client.get(QStringLiteral("/api/v1/boms/%1").arg(records.bomId), token),
        200,
        QStringLiteral("persisted BOM read")
    );
    require(bom.value(QStringLiteral("code")).toString().startsWith(config.prefix),
            QStringLiteral("persisted BOM does not belong to this E2E prefix"));
    require(!bom.value(QStringLiteral("isEnabled")).toBool(true),
            QStringLiteral("persisted BOM disable state was lost"));
    require(bom.value(QStringLiteral("items")).toArray().size() == 1,
            QStringLiteral("persisted BOM items were lost"));
    std::cout << "[PASS] customer, material and BOM persisted\n";
}

void runWorkflow(const WorkflowConfig& config) {
    HttpClient client(config.baseUrl, config.timeoutMs);
    std::optional<ManagedServer> server;
    if (!config.serverExecutable.isEmpty()) {
        server.emplace(config.serverExecutable, config.baseUrl, config.timeoutMs);
        server->migrate();
        server->start(client);
    } else {
        require(config.allowExternalNoRestart,
                QStringLiteral(
                    "--server-executable is required for migration/start/restart; "
                    "use --external-no-restart only for an already managed test server"
                ));
        expectStatus(client.get(QStringLiteral("/api/v1/health")), 200,
                     QStringLiteral("external server health check"));
        std::cout << "[PASS] external manage-server is ready\n";
    }

    const auto bootstrap = expectStatus(
        client.sendJson(
            QByteArrayLiteral("POST"),
            QStringLiteral("/api/v1/auth/bootstrap"),
            QJsonObject{
                {QStringLiteral("password"), config.temporaryPassword},
                {QStringLiteral("displayName"), config.prefix + QStringLiteral("-ADMIN")},
            }
        ),
        201,
        QStringLiteral("administrator bootstrap")
    );
    require(bootstrap.value(QStringLiteral("user")).toObject()
                .value(QStringLiteral("mustChangePassword")).toBool(),
            QStringLiteral("bootstrap administrator must require a password change"));
    std::cout << "[PASS] administrator bootstrapped\n";

    const auto temporaryToken = login(client, config.temporaryPassword, true);
    std::cout << "[PASS] temporary password login completed\n";
    if (config.strictAuthorization) {
        checkStrictAuthorizationBeforePasswordChange(client, temporaryToken);
    } else {
        std::cout << "[SKIP] strict authorization checks explicitly disabled\n";
    }

    const auto changed = expectStatus(
        client.sendJson(
            QByteArrayLiteral("POST"),
            QStringLiteral("/api/v1/auth/change-password"),
            QJsonObject{
                {QStringLiteral("currentPassword"), config.temporaryPassword},
                {QStringLiteral("newPassword"), config.permanentPassword},
            },
            temporaryToken
        ),
        200,
        QStringLiteral("password change")
    );
    require(!changed.value(QStringLiteral("user")).toObject()
                 .value(QStringLiteral("mustChangePassword")).toBool(true),
            QStringLiteral("password change did not clear the required flag"));
    std::cout << "[PASS] permanent password configured\n";

    const auto activeToken = login(client, config.permanentPassword, false);
    std::cout << "[PASS] permanent password login completed\n";
    auto records = createAndModifyBusinessData(client, config, activeToken);
    createQuoteDraft(client, config, activeToken, records);

    expectStatus(
        client.sendJson(
            QByteArrayLiteral("POST"),
            QStringLiteral("/api/v1/auth/logout"),
            QJsonObject{},
            activeToken
        ),
        200,
        QStringLiteral("logout")
    );
    expectAuthError(
        client.get(QStringLiteral("/api/v1/auth/me"), activeToken),
        401,
        QStringLiteral("unauthorized"),
        QStringLiteral("logged-out token reuse")
    );
    if (config.strictAuthorization) {
        expectAuthError(
            client.get(QStringLiteral("/api/v1/materials"), activeToken),
            401,
            QStringLiteral("unauthorized"),
            QStringLiteral("logged-out business token reuse")
        );
    }
    std::cout << "[PASS] logout invalidated the old token\n";

    if (server.has_value()) {
        server->restart(client);
    } else {
        std::cout << "[SKIP] external server restart is controlled by the caller\n";
    }
    const auto afterRestartToken = login(client, config.permanentPassword, false);
    verifyPersistedData(client, config, records, afterRestartToken);
    completeQuoteLifecycleAfterRestart(
        client, config, records, afterRestartToken
    );
    std::cout << "[PASS] complete backend E2E workflow\n";
}

void runSelfTest() {
    const QUrl safeUrl(QStringLiteral("http://127.0.0.1:18080"));
    require(
        safetyError(QStringLiteral("1"), QStringLiteral("manage_e2e_test"),
                    safeUrl, QStringLiteral("E2E-SELFTEST-1234")).isEmpty(),
        QStringLiteral("safe preflight input should be accepted")
    );
    require(
        !safetyError(QStringLiteral("0"), QStringLiteral("manage_e2e_test"),
                     safeUrl, QStringLiteral("E2E-SELFTEST-1234")).isEmpty(),
        QStringLiteral("missing MANAGE_E2E gate should be rejected")
    );
    require(
        !safetyError(QStringLiteral("1"), QStringLiteral("manage"), safeUrl,
                     QStringLiteral("E2E-SELFTEST-1234")).isEmpty(),
        QStringLiteral("non-test database should be rejected")
    );
    require(
        !safetyError(QStringLiteral("1"), QStringLiteral("manage_e2e_test"),
                     QUrl(QStringLiteral("https://example.com:443")),
                     QStringLiteral("E2E-SELFTEST-1234")).isEmpty(),
        QStringLiteral("remote base URL should be rejected")
    );
    const auto generated = uniquePrefix();
    require(generated.startsWith(QStringLiteral("E2E-")),
            QStringLiteral("generated prefix should be isolated"));
    require(
        safetyError(QStringLiteral("1"), QStringLiteral("manage_e2e_test"),
                    safeUrl, generated).isEmpty(),
        QStringLiteral("generated prefix should pass preflight")
    );
    std::cout << "[PASS] E2E safety gate, database suffix, URL and prefix self-tests\n";
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("manage-backend-e2e"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Destructive-safe black-box workflow test for manage-server")
    );
    parser.addHelpOption();
    const QCommandLineOption selfTestOption(
        QStringLiteral("self-test"),
        QStringLiteral("Run local preflight logic tests without a server or database")
    );
    const QCommandLineOption preflightOnlyOption(
        QStringLiteral("preflight-only"),
        QStringLiteral("Validate the real-run safety gate and exit")
    );
    const QCommandLineOption baseUrlOption(
        QStringLiteral("base-url"),
        QStringLiteral("Target server URL (local HTTP only)"),
        QStringLiteral("url"),
        QStringLiteral("http://127.0.0.1:18080")
    );
    const QCommandLineOption serverExecutableOption(
        QStringLiteral("server-executable"),
        QStringLiteral("manage-server executable to migrate, start and restart"),
        QStringLiteral("path")
    );
    const QCommandLineOption prefixOption(
        QStringLiteral("prefix"),
        QStringLiteral("Unique E2E data prefix; generated when omitted"),
        QStringLiteral("E2E-prefix")
    );
    const QCommandLineOption legacyAuthorizationOption(
        QStringLiteral("legacy-unprotected-routes"),
        QStringLiteral(
            "Explicit diagnostic mode for the pre-authorization server; not acceptance"
        )
    );
    const QCommandLineOption strictAuthorizationOption(
        QStringLiteral("strict-authorization"),
        QStringLiteral(
            "Explicitly require the default 401/403 business authorization assertions"
        )
    );
    const QCommandLineOption externalNoRestartOption(
        QStringLiteral("external-no-restart"),
        QStringLiteral(
            "Target an already running external-local server; restart is reported as skipped"
        )
    );
    const QCommandLineOption timeoutOption(
        QStringLiteral("timeout-ms"),
        QStringLiteral("Per-request and server-start timeout"),
        QStringLiteral("milliseconds"),
        QStringLiteral("10000")
    );
    parser.addOptions({
        selfTestOption,
        preflightOnlyOption,
        baseUrlOption,
        serverExecutableOption,
        prefixOption,
        strictAuthorizationOption,
        legacyAuthorizationOption,
        externalNoRestartOption,
        timeoutOption,
    });
    parser.process(application);

    try {
        if (parser.isSet(selfTestOption)) {
            runSelfTest();
            return EXIT_SUCCESS;
        }

        WorkflowConfig config;
        require(
            !(parser.isSet(strictAuthorizationOption) &&
              parser.isSet(legacyAuthorizationOption)),
            QStringLiteral(
                "--strict-authorization and --legacy-unprotected-routes cannot be combined"
            )
        );
        config.baseUrl = QUrl(parser.value(baseUrlOption));
        config.serverExecutable = parser.value(serverExecutableOption);
        config.prefix = parser.isSet(prefixOption)
                            ? parser.value(prefixOption).toUpper()
                            : uniquePrefix();
        config.strictAuthorization = !parser.isSet(legacyAuthorizationOption);
        config.allowExternalNoRestart = parser.isSet(externalNoRestartOption);
        bool timeoutOk = false;
        config.timeoutMs = parser.value(timeoutOption).toInt(&timeoutOk);
        require(timeoutOk && config.timeoutMs >= 1'000 && config.timeoutMs <= 120'000,
                QStringLiteral("--timeout-ms must be between 1000 and 120000"));

        const auto preflightError = safetyError(
            qEnvironmentVariable("MANAGE_E2E"),
            qEnvironmentVariable("MANAGE_DB_NAME"),
            config.baseUrl,
            config.prefix
        );
        require(preflightError.isEmpty(), preflightError);
        if (!config.serverExecutable.isEmpty()) {
            const QFileInfo serverInfo(config.serverExecutable);
            require(serverInfo.exists() && serverInfo.isFile(),
                    QStringLiteral("--server-executable does not exist"));
        }

        if (parser.isSet(preflightOnlyOption)) {
            std::cout << "[PASS] real E2E preflight checks\n";
            return EXIT_SUCCESS;
        }

        config.temporaryPassword = config.prefix + QStringLiteral("-Temp!9");
        config.permanentPassword = config.prefix + QStringLiteral("-Permanent!9");
        std::cout << "[INFO] data prefix: "
                  << config.prefix.toStdString() << '\n';
        std::cout << "[INFO] authorization mode: "
                  << (config.strictAuthorization ? "strict" : "legacy diagnostic")
                  << '\n';
        runWorkflow(config);
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
