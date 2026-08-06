#include "manage/desktop/api_client.h"
#include "manage/desktop/quote_management_widget.h"

#include <QApplication>
#include <QAbstractButton>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponder>
#include <QHttpServerResponse>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTextEdit>
#include <QThread>
#include <QTimer>
#include <QUrlQuery>

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
using manage::desktop::ApiResponse;
using manage::desktop::QuoteManagementWidget;
using StatusCode = QHttpServerResponder::StatusCode;

class TestFailure final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void require(bool value, const std::string& message) {
    if (!value) throw TestFailure(message);
}

bool waitUntil(const std::function<bool()>& predicate, int timeoutMs = 5'000) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(2);
    }
    return predicate();
}

template <typename T>
T* child(QObject& root, const char* name) {
    auto* result = root.findChild<T*>(QString::fromLatin1(name));
    require(result != nullptr, std::string("missing child: ") + name);
    return result;
}

ApiResponse waitFor(const std::function<void(ApiClient::Callback)>& start) {
    std::optional<ApiResponse> result;
    start([&](ApiResponse response) { result = std::move(response); });
    require(waitUntil([&] { return result.has_value(); }), "API request timed out");
    return std::move(*result);
}

QJsonObject bodyOf(const QHttpServerRequest& request) {
    return QJsonDocument::fromJson(request.body()).object();
}

struct TestApi final {
    QHttpServer server;
    QTcpServer tcp;
    QString role{QStringLiteral("admin")};
    bool temporaryPassword{};
    bool failConflict{};
    bool failForbidden{};
    int quoteListCount{};
    int detailCount{};
    int createCount{};
    int updateCount{};
    int statusCount{};
    int cloneCount{};
    int deleteCount{};
    int lookupCount{};
    qint64 nextId{2};
    QJsonObject lastBody;
    QJsonObject lastCreateBody;
    QJsonObject lastUpdateBody;
    QString lastPath;
    QString lastDeletePath;
    QString lastMethod;
    QString materialSearch;
    QJsonObject current;

    TestApi() : current(makeQuote(1, QStringLiteral("Q-20260805-1"), QStringLiteral("draft"), 1)) {
        server.route(QStringLiteral("/api/v1/auth/login"), QHttpServerRequest::Method::Post,
            [this](const QHttpServerRequest&) { return QHttpServerResponse(loginResponse()); });
        server.route(QStringLiteral("/api/v1/auth/change-password"), QHttpServerRequest::Method::Post,
            [this](const QHttpServerRequest&) {
                temporaryPassword = false;
                return QHttpServerResponse(QJsonObject{{QStringLiteral("user"), user()}});
            });
        server.route(QStringLiteral("/api/v1/customers"), QHttpServerRequest::Method::Get,
            [this](const QHttpServerRequest&) {
                ++lookupCount;
                return QHttpServerResponse(QJsonObject{{QStringLiteral("items"), QJsonArray{
                    QJsonObject{{QStringLiteral("id"), 11}, {QStringLiteral("name"), QStringLiteral("中文客户")}},
                }}});
            });
        server.route(QStringLiteral("/api/v1/boms"), QHttpServerRequest::Method::Get,
            [this](const QHttpServerRequest&) {
                ++lookupCount;
                return QHttpServerResponse(QJsonObject{{QStringLiteral("items"), QJsonArray{
                    QJsonObject{{QStringLiteral("id"), 21}, {QStringLiteral("code"), QStringLiteral("BOM-21")}, {QStringLiteral("name"), QStringLiteral("测试总成")}},
                }}});
            });
        server.route(QStringLiteral("/api/v1/boms/<arg>"), QHttpServerRequest::Method::Get,
            [this](qint64) {
                ++lookupCount;
                return QHttpServerResponse(QJsonObject{
                    {QStringLiteral("id"), 21},
                    {QStringLiteral("code"), QStringLiteral("BOM-21")},
                    {QStringLiteral("name"), QStringLiteral("测试总成")},
                    {QStringLiteral("items"), QJsonArray{QJsonObject{
                        {QStringLiteral("lineNo"), 1}, {QStringLiteral("materialId"), 31},
                        {QStringLiteral("quantityMicros"), 2'000'000}, {QStringLiteral("notes"), QStringLiteral("BOM 行")},
                    }}}
                });
            });
        server.route(QStringLiteral("/api/v1/materials"), QHttpServerRequest::Method::Get,
            [this](const QHttpServerRequest& request) {
                ++lookupCount;
                materialSearch = QUrlQuery(request.url()).queryItemValue(QStringLiteral("search"));
                return QHttpServerResponse(QJsonObject{{QStringLiteral("items"), QJsonArray{
                    QJsonObject{{QStringLiteral("id"), 31}, {QStringLiteral("code"), QStringLiteral("MAT-31")},
                        {QStringLiteral("name"), QStringLiteral("测试钢材")}, {QStringLiteral("specification"), QStringLiteral("10mm")},
                        {QStringLiteral("unit"), QStringLiteral("kg")}, {QStringLiteral("currentUnitPriceCents"), 1234}},
                    }}});
            });
        server.route(QStringLiteral("/api/v1/materials/<arg>"), QHttpServerRequest::Method::Get,
            [this](qint64) {
                ++lookupCount;
                return QHttpServerResponse(QJsonObject{
                    {QStringLiteral("id"), 31}, {QStringLiteral("code"), QStringLiteral("MAT-31")},
                    {QStringLiteral("name"), QStringLiteral("测试钢材")}, {QStringLiteral("specification"), QStringLiteral("10mm")},
                    {QStringLiteral("unit"), QStringLiteral("kg")}, {QStringLiteral("currentUnitPriceCents"), 1234},
                    {QStringLiteral("isEnabled"), true},
                });
            });
        server.route(QStringLiteral("/api/v1/quotes"), QHttpServerRequest::Method::Get,
            [this](const QHttpServerRequest& request) {
                capture(request, QStringLiteral("GET"));
                ++quoteListCount;
                return QHttpServerResponse(QJsonObject{
                    {QStringLiteral("items"), QJsonArray{summary(current)}},
                    {QStringLiteral("total"), 41}, {QStringLiteral("page"), request.query().queryItemValue(QStringLiteral("page")).toInt()},
                    {QStringLiteral("pageSize"), 20},
                });
            });
        server.route(QStringLiteral("/api/v1/quotes/<arg>/status"), QHttpServerRequest::Method::Patch,
            [this](qint64, const QHttpServerRequest& request) {
                capture(request, QStringLiteral("PATCH"));
                ++statusCount;
                if (failForbidden) {
                    failForbidden = false;
                    return QHttpServerResponse(QJsonObject{{QStringLiteral("error"), QStringLiteral("forbidden")}, {QStringLiteral("message"), QStringLiteral("denied")}}, StatusCode::Forbidden);
                }
                current.insert(QStringLiteral("status"), lastBody.value(QStringLiteral("status")));
                current.insert(QStringLiteral("revision"), current.value(QStringLiteral("revision")).toInt() + 1);
                return QHttpServerResponse(current);
            });
        server.route(QStringLiteral("/api/v1/quotes/<arg>/clone"), QHttpServerRequest::Method::Post,
            [this](qint64, const QHttpServerRequest& request) {
                capture(request, QStringLiteral("POST"));
                ++cloneCount;
                current = makeQuote(nextId, QStringLiteral("Q-20260805-%1").arg(nextId), QStringLiteral("draft"), 1);
                current.insert(QStringLiteral("sourceQuoteId"), 1);
                ++nextId;
                return QHttpServerResponse(current, StatusCode::Created);
            });
        server.route(QStringLiteral("/api/v1/quotes/<arg>"), QHttpServerRequest::Method::Get,
            [this](qint64, const QHttpServerRequest& request) {
                capture(request, QStringLiteral("GET"));
                ++detailCount;
                return QHttpServerResponse(current);
            });
        server.route(QStringLiteral("/api/v1/quotes"), QHttpServerRequest::Method::Post,
            [this](const QHttpServerRequest& request) {
                capture(request, QStringLiteral("POST"));
                lastCreateBody = lastBody;
                ++createCount;
                current = fromPayload(nextId, QStringLiteral("Q-20260805-%1").arg(nextId), lastBody, 1);
                ++nextId;
                return QHttpServerResponse(current, StatusCode::Created);
            });
        server.route(QStringLiteral("/api/v1/quotes/<arg>"), QHttpServerRequest::Method::Put,
            [this](qint64, const QHttpServerRequest& request) {
                capture(request, QStringLiteral("PUT"));
                lastUpdateBody = lastBody;
                ++updateCount;
                if (failConflict) {
                    failConflict = false;
                    return QHttpServerResponse(QJsonObject{{QStringLiteral("error"), QStringLiteral("revision_conflict")}, {QStringLiteral("message"), QStringLiteral("stale")}}, StatusCode::Conflict);
                }
                current = fromPayload(current.value(QStringLiteral("id")).toInteger(), current.value(QStringLiteral("quoteNumber")).toString(), lastBody, current.value(QStringLiteral("revision")).toInt() + 1);
                return QHttpServerResponse(current);
            });
        server.route(QStringLiteral("/api/v1/quotes/<arg>"), QHttpServerRequest::Method::Delete,
            [this](qint64, const QHttpServerRequest& request) {
                capture(request, QStringLiteral("DELETE"));
                lastDeletePath = request.url().toString();
                ++deleteCount;
                return QHttpServerResponse(QJsonObject{});
            });
        if (!tcp.listen(QHostAddress::LocalHost, 0) || !server.bind(&tcp)) {
            throw TestFailure("failed to start quote desktop test API");
        }
    }

    QUrl baseUrl() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(tcp.serverPort())); }

    QJsonObject user() const {
        return QJsonObject{{QStringLiteral("username"), QStringLiteral("tester")}, {QStringLiteral("role"), role},
            {QStringLiteral("mustChangePassword"), temporaryPassword}};
    }
    QJsonObject loginResponse() const {
        return QJsonObject{{QStringLiteral("accessToken"), QStringLiteral("quote-token")},
            {QStringLiteral("expiresAt"), QStringLiteral("2026-08-06T00:00:00Z")}, {QStringLiteral("user"), user()}};
    }
    static QJsonObject makeQuote(qint64 id, const QString& number, const QString& status, int revision) {
        return QJsonObject{
            {QStringLiteral("id"), id}, {QStringLiteral("quoteNumber"), number}, {QStringLiteral("status"), status},
            {QStringLiteral("customerId"), 11}, {QStringLiteral("customerName"), QStringLiteral("中文客户")},
            {QStringLiteral("bomTemplateId"), 21}, {QStringLiteral("bomName"), QStringLiteral("测试总成")},
            {QStringLiteral("freightCents"), 1000}, {QStringLiteral("otherFeesCents"), 200},
            {QStringLiteral("markupBasisPoints"), 2000}, {QStringLiteral("taxBasisPoints"), 1300},
            {QStringLiteral("materialCostCents"), 3085}, {QStringLiteral("priceBeforeTaxCents"), 5142},
            {QStringLiteral("priceWithTaxCents"), 5810}, {QStringLiteral("notes"), QStringLiteral("初始备注")},
            {QStringLiteral("revision"), revision}, {QStringLiteral("createdAt"), QStringLiteral("2026-08-05T10:00:00Z")},
            {QStringLiteral("updatedAt"), QStringLiteral("2026-08-05T10:01:00Z")},
            {QStringLiteral("items"), QJsonArray{QJsonObject{
                {QStringLiteral("materialId"), 31}, {QStringLiteral("materialCode"), QStringLiteral("MAT-31")},
                {QStringLiteral("materialName"), QStringLiteral("测试钢材")}, {QStringLiteral("specification"), QStringLiteral("10mm")},
                {QStringLiteral("unit"), QStringLiteral("kg")}, {QStringLiteral("quantityMicros"), 2'500'000},
                {QStringLiteral("unitPriceCents"), 1234}, {QStringLiteral("subtotalCents"), 3085},
                {QStringLiteral("notes"), QStringLiteral("快照行")},
            }}},
        };
    }
    static QJsonObject summary(const QJsonObject& quote) {
        auto result = quote;
        result.remove(QStringLiteral("items"));
        return result;
    }
    static QJsonObject fromPayload(qint64 id, const QString& number, const QJsonObject& payload, int revision) {
        auto result = makeQuote(id, number, QStringLiteral("draft"), revision);
        for (const auto& key : {QStringLiteral("customerId"), QStringLiteral("bomTemplateId"), QStringLiteral("bomQuantityMicros"), QStringLiteral("freightCents"),
                 QStringLiteral("otherFeesCents"), QStringLiteral("markupBasisPoints"), QStringLiteral("taxBasisPoints"), QStringLiteral("notes")}) {
            result.insert(key, payload.value(key));
        }
        QJsonArray expanded;
        for (const auto& value : payload.value(QStringLiteral("items")).toArray()) {
            auto item = value.toObject();
            item.insert(QStringLiteral("materialCode"), QStringLiteral("MAT-31"));
            item.insert(QStringLiteral("materialName"), QStringLiteral("测试钢材"));
            item.insert(QStringLiteral("specification"), QStringLiteral("10mm"));
            item.insert(QStringLiteral("unit"), QStringLiteral("kg"));
            expanded.append(item);
        }
        result.insert(QStringLiteral("items"), expanded);
        return result;
    }
    void capture(const QHttpServerRequest& request, const QString& method) {
        lastBody = bodyOf(request);
        lastPath = request.url().toString();
        lastMethod = method;
    }
};

void login(ApiClient& client) {
    const auto response = waitFor([&](ApiClient::Callback callback) {
        client.login(QStringLiteral("tester"), QStringLiteral("Password-1234"), std::move(callback));
    });
    require(response.succeeded(), "login must succeed");
}

void adminLifecycle() {
    TestApi api;
    ApiClient client(api.baseUrl());
    login(client);
    QuoteManagementWidget widget(&client);
    widget.show();
    auto* table = child<QTableWidget>(widget, "quoteListTable");
    require(waitUntil([&] { return table->rowCount() == 1 && api.lookupCount >= 3; }), "list and lookup requests must load");
    require(table->item(0, 2)->text() == QStringLiteral("草稿"), "status must be displayed in Chinese");
    require(table->item(0, 3)->text() == QStringLiteral("¥58.10"), "list total must come from API");

    table->selectRow(0);
    child<QPushButton>(widget, "quoteViewButton")->click();
    require(waitUntil([&] { return api.detailCount >= 1 && child<QLabel>(widget, "quoteNumberLabel")->text() == QStringLiteral("Q-20260805-1"); }), "detail must load");
    require(child<QLabel>(widget, "quoteSavedWithTaxLabel")->text() == QStringLiteral("¥58.10"), "detail total must use server response");

    child<QPushButton>(widget, "quoteNewButton")->click();
    auto* bomCombo = child<QComboBox>(widget, "quoteBomCombo");
    auto* bomQuantity = child<QDoubleSpinBox>(widget, "quoteBomQuantitySpin");
    bomQuantity->setValue(3.0);
    bomCombo->setCurrentIndex(bomCombo->findData(21, Qt::UserRole));
    auto* items = child<QTableWidget>(widget, "quoteSavedItemsTable");
    require(waitUntil([&] { return items->rowCount() == 1; }), "selecting a BOM must load its material rows");
    require(items->item(0, 3)->text() == QStringLiteral("6"), "BOM quantity must expand material quantities");
    const auto lookupBeforeSearch = api.lookupCount;
    child<QLineEdit>(widget, "quoteSavedMaterialSearchEdit")->setText(QStringLiteral("特殊钢材"));
    child<QPushButton>(widget, "quoteSavedMaterialSearchButton")->click();
    require(waitUntil([&] { return api.lookupCount > lookupBeforeSearch && api.materialSearch == QStringLiteral("特殊钢材"); }), "material search must reach backend for large catalogs");
    child<QPushButton>(widget, "quoteSavedAddItemButton")->click();
    require(items->rowCount() == 2, "material picker must append to BOM material rows");
    items->item(0, 3)->setText(QStringLiteral("2.500001"));
    items->item(0, 4)->setText(QStringLiteral("12.34"));
    child<QDoubleSpinBox>(widget, "quoteSavedFreightSpin")->setValue(10.25);
    child<QDoubleSpinBox>(widget, "quoteSavedMarkupSpin")->setValue(20.50);
    child<QDoubleSpinBox>(widget, "quoteSavedTaxSpin")->setValue(13.00);
    child<QTextEdit>(widget, "quoteSavedNotesEdit")->setPlainText(QStringLiteral("新草稿"));
    child<QPushButton>(widget, "quoteSavedSaveButton")->click();
    require(waitUntil([&] { return api.createCount == 1; }), "new draft must POST");
    require(api.lastCreateBody.value(QStringLiteral("revision")).isUndefined(), "POST must not send revision");
    require(api.lastCreateBody.value(QStringLiteral("bomQuantityMicros")).toInteger() == 3'000'000,
            "POST must preserve BOM quantity");
    const auto postedItem = api.lastCreateBody.value(QStringLiteral("items")).toArray().first().toObject();
    require(postedItem.value(QStringLiteral("quantityMicros")).toInteger() == 2'500'001, "quantity must convert to micros exactly");
    require(postedItem.value(QStringLiteral("unitPriceCents")).toInteger() == 1234, "price must convert to cents exactly");
    require(api.lastCreateBody.value(QStringLiteral("markupBasisPoints")).toInteger() == 2050, "percent must convert to basis points");
    require(waitUntil([&] { return child<QLabel>(widget, "quoteNumberLabel")->text() == QStringLiteral("Q-20260805-2"); }), "created detail must display");

    require(waitUntil([&] { return table->rowCount() == 1 && child<QPushButton>(widget, "quoteSearchButton")->isEnabled(); }), "list must refresh after create");
    table->selectRow(0);
    require(waitUntil([&] { return child<QPushButton>(widget, "quoteEditButton")->isEnabled(); }), "refreshed draft must be selectable for editing");
    child<QPushButton>(widget, "quoteEditButton")->click();
    require(waitUntil([&] { return child<QPushButton>(widget, "quoteSavedSaveButton")->isEnabled(); }), "draft editor must open");
    api.failConflict = true;
    child<QPushButton>(widget, "quoteSavedSaveButton")->click();
    require(waitUntil([&] { return child<QLabel>(widget, "quoteManagementStatusLabel")->text().contains(QStringLiteral("冲突")); }), "409 must be explained in Chinese");
    child<QPushButton>(widget, "quoteSavedSaveButton")->click();
    require(waitUntil([&] { return api.updateCount == 2 && child<QLabel>(widget, "quoteRevisionLabel")->text() == QStringLiteral("2"); }), "PUT must update draft with revision");
    require(api.lastUpdateBody.value(QStringLiteral("revision")).toInteger() == 1, "PUT must send current revision");

    require(waitUntil([&] { return child<QPushButton>(widget, "quoteIssueButton")->isEnabled(); }), "issue must enable after list refresh");
    child<QPushButton>(widget, "quoteIssueButton")->click();
    require(waitUntil([&] { return api.statusCount == 1 && child<QLabel>(widget, "quoteStateLabel")->text() == QStringLiteral("已发布"); }), "draft must issue through PATCH");
    require(waitUntil([&] { return child<QPushButton>(widget, "quoteVoidButton")->isEnabled(); }), "void must enable after issue refresh");
    api.failForbidden = true;
    child<QPushButton>(widget, "quoteVoidButton")->click();
    require(waitUntil([&] { return child<QLabel>(widget, "quoteManagementStatusLabel")->text().contains(QStringLiteral("没有报价操作权限")); }), "403 must be friendly Chinese");
    child<QPushButton>(widget, "quoteVoidButton")->click();
    require(waitUntil([&] { return api.statusCount == 3 && child<QLabel>(widget, "quoteStateLabel")->text() == QStringLiteral("已作废"); }), "issued quote must void");

    require(waitUntil([&] { return child<QPushButton>(widget, "quoteCloneButton")->isEnabled(); }), "clone must enable after void refresh");
    child<QPushButton>(widget, "quoteCloneButton")->click();
    require(waitUntil([&] { return api.cloneCount == 1 && child<QLabel>(widget, "quoteStateLabel")->text().contains(QStringLiteral("草稿")); }), "any status must clone to draft");
    require(waitUntil([&] { return child<QPushButton>(widget, "quoteDeleteButton")->isEnabled(); }), "delete must enable after clone refresh");
    QTimer::singleShot(50, [] {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            if (auto* yes = box->button(QMessageBox::Yes)) yes->click();
        }
    });
    child<QPushButton>(widget, "quoteDeleteButton")->click();
    require(waitUntil([&] { return api.deleteCount == 1; }), "confirmed draft deletion must use DELETE");
    require(api.lastDeletePath.contains(QStringLiteral("revision=1")), "DELETE must send revision query");
}

void rolesAndTemporaryPassword() {
    {
        TestApi api;
        api.temporaryPassword = true;
        ApiClient client(api.baseUrl());
        login(client);
        QuoteManagementWidget widget(&client);
        widget.show();
        QCoreApplication::processEvents();
        require(api.quoteListCount == 0 && api.lookupCount == 0, "temporary password must block all quote requests");
        require(child<QLabel>(widget, "quoteManagementStatusLabel")->text().contains(QStringLiteral("临时密码")), "temporary password guidance must be Chinese");
        const auto response = waitFor([&](ApiClient::Callback callback) {
            client.changePassword(QStringLiteral("Password-1234"), QStringLiteral("Password-5678"), std::move(callback));
        });
        require(response.succeeded(), "password change must succeed");
        require(waitUntil([&] { return api.quoteListCount > 0; }), "quote list must recover after password change");
    }
    {
        TestApi api;
        api.role = QStringLiteral("viewer");
        ApiClient client(api.baseUrl());
        login(client);
        QuoteManagementWidget widget(&client);
        widget.show();
        auto* table = child<QTableWidget>(widget, "quoteListTable");
        require(waitUntil([&] { return table->rowCount() == 1; }), "viewer must read list");
        table->selectRow(0);
        require(child<QPushButton>(widget, "quoteViewButton")->isEnabled(), "viewer must view details");
        require(!child<QPushButton>(widget, "quoteNewButton")->isEnabled(), "viewer must not create");
        require(!child<QPushButton>(widget, "quoteEditButton")->isEnabled(), "viewer must not edit");
    }
    {
        TestApi api;
        api.role = QStringLiteral("quoter");
        ApiClient client(api.baseUrl());
        login(client);
        QuoteManagementWidget widget(&client);
        widget.show();
        require(waitUntil([&] { return child<QPushButton>(widget, "quoteNewButton")->isEnabled(); }), "quoter must create quotes");
    }
}

void networkErrorIsFriendly() {
    TestApi api;
    ApiClient client(api.baseUrl());
    login(client);
    QuoteManagementWidget widget(&client);
    widget.show();
    require(waitUntil([&] { return api.quoteListCount > 0 && child<QPushButton>(widget, "quoteSearchButton")->isEnabled(); }), "initial list must load");
    api.tcp.close();
    for (auto* socket : api.tcp.findChildren<QTcpSocket*>()) socket->abort();
    for (auto* socket : api.server.findChildren<QTcpSocket*>()) socket->abort();
    child<QPushButton>(widget, "quoteSearchButton")->click();
    require(waitUntil([&] { return child<QLabel>(widget, "quoteManagementStatusLabel")->text().contains(QStringLiteral("网络连接失败")); }), "network failure must be friendly Chinese");
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    const std::vector<std::pair<std::string, std::function<void()>>> tests{
        {"admin lifecycle and API payload contract", adminLifecycle},
        {"roles and temporary password gate", rolesAndTemporaryPassword},
        {"Chinese network error", networkErrorIsFriendly},
    };
    int passed = 0;
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
    return passed == static_cast<int>(tests.size()) ? EXIT_SUCCESS : EXIT_FAILURE;
}
