#include "manage/desktop/api_client.h"
#include "manage/desktop/bom_quote_widget.h"

#include <QApplication>
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
#include <QPushButton>
#include <QTableWidget>
#include <QTcpServer>
#include <QThread>
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
using manage::desktop::BomQuoteWidget;
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

bool waitUntil(const std::function<bool()>& condition, int timeoutMs = 5'000) {
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(2);
    }
    return condition();
}

ApiResponse waitFor(const std::function<void(ApiClient::Callback)>& begin) {
    std::optional<ApiResponse> result;
    begin([&result](ApiResponse response) { result = std::move(response); });
    require(waitUntil([&result]() { return result.has_value(); }),
            "API callback timed out");
    return std::move(*result);
}

QJsonObject requestObject(const QHttpServerRequest& request) {
    return QJsonDocument::fromJson(request.body()).object();
}

QJsonArray expandedItems(const QJsonArray& input) {
    QJsonArray result;
    for (const auto& value : input) {
        const auto source = value.toObject();
        result.append(QJsonObject{
            {QStringLiteral("id"), 101},
            {QStringLiteral("lineNo"), source.value(QStringLiteral("lineNo"))},
            {QStringLiteral("materialId"), source.value(QStringLiteral("materialId"))},
            {QStringLiteral("materialCode"), QStringLiteral("MAT-42")},
            {QStringLiteral("materialName"), QStringLiteral("测试钢材")},
            {QStringLiteral("materialSpecification"), QStringLiteral("10mm")},
            {QStringLiteral("materialUnit"), QStringLiteral("kg")},
            {QStringLiteral("quantityMicros"), source.value(QStringLiteral("quantityMicros"))},
            {QStringLiteral("notes"), source.value(QStringLiteral("notes"))},
        });
    }
    return result;
}

struct TestApi final {
    QHttpServer server;
    QTcpServer tcpServer;
    QString loginRole{QStringLiteral("admin")};
    QByteArray lastAuthorization;
    QJsonObject lastBody;
    QJsonObject lastCreateBody;
    QJsonObject lastUpdateBody;
    QJsonObject lastReplaceBody;
    QJsonObject lastToggleBody;
    QJsonObject lastQuoteBody;
    QString lastPath;
    QString lastSearch;
    QString lastMaterialSearch;
    int createCount{};
    int detailCount{};
    int updateCount{};
    int replaceCount{};
    int toggleCount{};
    int quoteCount{};
    bool failNextUpdate{};
    qint64 bomId{1};
    int revision{1};
    bool enabled{true};
    QString code{QStringLiteral("BOM-001")};
    QString name{QStringLiteral("初始总成")};
    QString description{QStringLiteral("测试 BOM")};
    QJsonArray items{
        QJsonObject{
            {QStringLiteral("lineNo"), 10},
            {QStringLiteral("materialId"), 42},
            {QStringLiteral("quantityMicros"), 1'000'000},
            {QStringLiteral("notes"), QStringLiteral("初始")},
        },
    };

    TestApi() {
        server.route(
            QStringLiteral("/api/v1/auth/login"),
            QHttpServerRequest::Method::Post,
            [this](const QHttpServerRequest&) {
                return QHttpServerResponse(QJsonObject{
                    {QStringLiteral("accessToken"), QStringLiteral("bom-quote-token")},
                    {QStringLiteral("expiresAt"), QStringLiteral("2026-08-06T12:00:00Z")},
                    {QStringLiteral("user"), QJsonObject{
                        {QStringLiteral("username"), QStringLiteral("tester")},
                        {QStringLiteral("role"), loginRole},
                        {QStringLiteral("mustChangePassword"), false},
                    }},
                });
            }
        );
        server.route(
            QStringLiteral("/api/v1/materials"),
            QHttpServerRequest::Method::Get,
            [this](const QHttpServerRequest& request) {
                capture(request);
                lastMaterialSearch = QUrlQuery(request.url())
                                         .queryItemValue(QStringLiteral("search"));
                const auto searched = !lastMaterialSearch.isEmpty();
                return QHttpServerResponse(QJsonObject{
                    {QStringLiteral("items"), QJsonArray{
                        QJsonObject{
                            {QStringLiteral("id"), searched ? 2000 : 42},
                            {QStringLiteral("code"), searched
                                ? QStringLiteral("MAT-2000")
                                : QStringLiteral("MAT-42")},
                            {QStringLiteral("name"), searched
                                ? QStringLiteral("第两千条特殊钢")
                                : QStringLiteral("测试钢材")},
                            {QStringLiteral("currentUnitPriceCents"), searched ? 5678 : 1234},
                            {QStringLiteral("isEnabled"), true},
                        },
                    }},
                    {QStringLiteral("total"), 1},
                });
            }
        );
        server.route(
            QStringLiteral("/api/v1/boms"),
            QHttpServerRequest::Method::Get,
            [this](const QHttpServerRequest& request) {
                capture(request);
                lastSearch = QUrlQuery(request.url())
                                 .queryItemValue(QStringLiteral("search"));
                return QHttpServerResponse(QJsonObject{
                    {QStringLiteral("items"), QJsonArray{summary()}},
                    {QStringLiteral("total"), 1},
                    {QStringLiteral("page"), 1},
                    {QStringLiteral("pageSize"), 100},
                });
            }
        );
        server.route(
            QStringLiteral("/api/v1/boms/<arg>"),
            QHttpServerRequest::Method::Get,
            [this](qint64, const QHttpServerRequest& request) {
                capture(request);
                ++detailCount;
                return QHttpServerResponse(detail());
            }
        );
        server.route(
            QStringLiteral("/api/v1/boms"),
            QHttpServerRequest::Method::Post,
            [this](const QHttpServerRequest& request) {
                capture(request);
                lastCreateBody = lastBody;
                ++createCount;
                bomId = 2;
                revision = 1;
                enabled = true;
                applyMetadata(lastBody);
                items = lastBody.value(QStringLiteral("items")).toArray();
                return QHttpServerResponse(detail(), StatusCode::Created);
            }
        );
        server.route(
            QStringLiteral("/api/v1/boms/<arg>"),
            QHttpServerRequest::Method::Put,
            [this](qint64, const QHttpServerRequest& request) {
                capture(request);
                lastUpdateBody = lastBody;
                ++updateCount;
                if (failNextUpdate) {
                    failNextUpdate = false;
                    return QHttpServerResponse(
                        QJsonObject{
                            {QStringLiteral("error"), QStringLiteral("revision_conflict")},
                            {QStringLiteral("message"), QStringLiteral("stale revision")},
                        },
                        StatusCode::Conflict
                    );
                }
                applyMetadata(lastBody);
                ++revision;
                return QHttpServerResponse(detail());
            }
        );
        server.route(
            QStringLiteral("/api/v1/boms/<arg>/items"),
            QHttpServerRequest::Method::Put,
            [this](qint64, const QHttpServerRequest& request) {
                capture(request);
                lastReplaceBody = lastBody;
                ++replaceCount;
                items = lastBody.value(QStringLiteral("items")).toArray();
                ++revision;
                return QHttpServerResponse(detail());
            }
        );
        server.route(
            QStringLiteral("/api/v1/boms/<arg>/enabled"),
            QHttpServerRequest::Method::Patch,
            [this](qint64, const QHttpServerRequest& request) {
                capture(request);
                lastToggleBody = lastBody;
                ++toggleCount;
                enabled = lastBody.value(QStringLiteral("isEnabled")).toBool();
                ++revision;
                return QHttpServerResponse(detail());
            }
        );
        server.route(
            QStringLiteral("/api/v1/quotes/calculate"),
            QHttpServerRequest::Method::Post,
            [this](const QHttpServerRequest& request) {
                capture(request);
                lastQuoteBody = lastBody;
                ++quoteCount;
                return QHttpServerResponse(QJsonObject{
                    {QStringLiteral("materialCostCents"), 3085},
                    {QStringLiteral("priceBeforeTaxCents"), 5000},
                    {QStringLiteral("priceWithTaxCents"), 5650},
                });
            }
        );

        if (!tcpServer.listen(QHostAddress::LocalHost, 0) ||
            !server.bind(&tcpServer)) {
            throw TestFailure("unable to start BOM quote widget test server");
        }
    }

    QUrl baseUrl() const {
        return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(tcpServer.serverPort()));
    }

    QJsonObject summary() const {
        return {
            {QStringLiteral("id"), bomId},
            {QStringLiteral("code"), code},
            {QStringLiteral("name"), name},
            {QStringLiteral("description"), description},
            {QStringLiteral("isEnabled"), enabled},
            {QStringLiteral("revision"), revision},
        };
    }

    QJsonObject detail() const {
        auto result = summary();
        result.insert(QStringLiteral("items"), expandedItems(items));
        return result;
    }

    void capture(const QHttpServerRequest& request) {
        lastAuthorization = request.value("Authorization");
        lastBody = requestObject(request);
        lastPath = request.url().path();
    }

    void applyMetadata(const QJsonObject& object) {
        code = object.value(QStringLiteral("code")).toString();
        name = object.value(QStringLiteral("name")).toString();
        description = object.value(QStringLiteral("description")).toString();
    }
};

template <typename T>
T* requiredChild(BomQuoteWidget& widget, const char* name) {
    auto* child = widget.findChild<T*>(QString::fromLatin1(name));
    require(child != nullptr, std::string("missing objectName: ") + name);
    return child;
}

void loginAs(ApiClient& client, TestApi& api, const QString& role) {
    api.loginRole = role;
    const auto response = waitFor([&client](ApiClient::Callback callback) {
        client.login(QStringLiteral("tester"), QStringLiteral("password-value"),
                     std::move(callback));
    });
    require(response.succeeded(), "test login must succeed");
}

void adminCanManageBomAndCalculateQuote(TestApi& api) {
    ApiClient client(api.baseUrl());
    loginAs(client, api, QStringLiteral("admin"));
    BomQuoteWidget widget(&client);
    widget.show();

    auto* list = requiredChild<QTableWidget>(widget, "bomListTable");
    auto* materialCombo = requiredChild<QComboBox>(widget, "bomMaterialCombo");
    auto* refresh = requiredChild<QPushButton>(widget, "bomRefreshButton");
    require(waitUntil([&]() {
        return list->rowCount() == 1 && materialCombo->count() == 1 && refresh->isEnabled();
    }), "initial BOM and material loads must finish");
    require(api.lastAuthorization == QByteArray("Bearer bom-quote-token"),
            "widget requests must use ApiClient Bearer token");

    auto* materialSearch = requiredChild<QLineEdit>(
        widget, "sharedMaterialSearchEdit"
    );
    auto* materialRefresh = requiredChild<QPushButton>(
        widget, "sharedMaterialRefreshButton"
    );
    auto* quoteCombo = requiredChild<QComboBox>(widget, "quoteMaterialCombo");
    materialSearch->setText(QStringLiteral("特殊钢 第两千条"));
    materialRefresh->click();
    require(waitUntil([&]() {
        return api.lastMaterialSearch == QStringLiteral("特殊钢 第两千条") &&
               materialCombo->currentData().toLongLong() == 2000 &&
               quoteCombo->currentData().toString() == QStringLiteral("MAT-2000") &&
               materialRefresh->isEnabled();
    }), "Chinese material search must refresh both material selectors");
    require(requiredChild<QLabel>(widget, "sharedMaterialSearchStatusLabel")
                ->text().contains(QStringLiteral("同步刷新")),
            "shared material search must report that both selectors refreshed");

    materialSearch->clear();
    materialRefresh->click();
    require(waitUntil([&]() {
        return api.lastMaterialSearch.isEmpty() &&
               materialCombo->currentData().toLongLong() == 42 &&
               quoteCombo->currentData().toString() == QStringLiteral("MAT-42") &&
               materialRefresh->isEnabled();
    }), "empty material search must restore the default enabled-material page");

    auto* search = requiredChild<QLineEdit>(widget, "bomSearchEdit");
    search->setText(QStringLiteral("总成 A"));
    refresh->click();
    require(waitUntil([&]() {
        return api.lastSearch == QStringLiteral("总成 A") &&
               list->rowCount() == 1 && refresh->isEnabled();
    }),
            "BOM search query must be UTF-8 encoded and sent");

    list->selectRow(0);
    auto* viewButton = requiredChild<QPushButton>(widget, "bomViewButton");
    require(waitUntil([&]() { return viewButton->isEnabled(); }),
            "selected BOM must enable the detail action");
    const auto detailCountBefore = api.detailCount;
    viewButton->click();
    require(waitUntil([&]() { return api.detailCount > detailCountBefore; }),
            "selected BOM detail request must reach the API");
    auto* codeEdit = requiredChild<QLineEdit>(widget, "bomCodeEdit");
    if (!waitUntil([&]() { return codeEdit->text() == api.code; })) {
        throw TestFailure(
            QStringLiteral(
                "selected BOM detail must load asynchronously; actual=%1 expected=%2 status=%3"
            )
                .arg(codeEdit->text())
                .arg(api.code)
                .arg(requiredChild<QLabel>(widget, "bomStatusLabel")->text())
                .toStdString()
        );
    }

    requiredChild<QPushButton>(widget, "bomNewButton")->click();
    codeEdit->setText(QStringLiteral("BOM-NEW"));
    requiredChild<QLineEdit>(widget, "bomNameEdit")
        ->setText(QStringLiteral("新总成"));
    requiredChild<QLineEdit>(widget, "bomDescriptionEdit")
        ->setText(QStringLiteral("新说明"));
    requiredChild<QPushButton>(widget, "bomAddItemButton")->click();
    auto* bomItems = requiredChild<QTableWidget>(widget, "bomItemsTable");
    require(bomItems->rowCount() == 1, "material picker must append one BOM item");
    bomItems->item(0, 3)->setText(QStringLiteral("1.500001"));
    requiredChild<QPushButton>(widget, "bomSaveButton")->click();
    require(waitUntil([&]() { return api.createCount == 1; }),
            "new BOM must use POST");
    const auto createdItem = api.lastCreateBody.value(QStringLiteral("items"))
                                 .toArray().first().toObject();
    require(createdItem.value(QStringLiteral("materialId")).toInteger() == 42,
            "BOM item must use selected backend material id");
    require(createdItem.value(QStringLiteral("quantityMicros")).toInteger() == 1'500'001,
            "BOM quantity must convert exactly to micros");
    require(waitUntil([&]() {
        const auto text = requiredChild<QLabel>(widget, "bomRevisionLabel")->text();
        return text.startsWith(QStringLiteral("1")) &&
               text.contains(QStringLiteral("ID 2")) &&
               requiredChild<QPushButton>(widget, "bomSaveButton")->isEnabled();
    }), "created BOM id and revision must be stored before editing");

    requiredChild<QLineEdit>(widget, "bomNameEdit")
        ->setText(QStringLiteral("新总成（修改）"));
    auto* saveButton = requiredChild<QPushButton>(widget, "bomSaveButton");
    saveButton->click();
    if (!waitUntil([&]() { return api.updateCount == 1; })) {
        throw TestFailure(
            QStringLiteral(
                "existing BOM must use PUT; enabled=%1 revision=%2 status=%3 lastPath=%4"
            )
                .arg(saveButton->isEnabled())
                .arg(requiredChild<QLabel>(widget, "bomRevisionLabel")->text())
                .arg(requiredChild<QLabel>(widget, "bomStatusLabel")->text())
                .arg(api.lastPath)
                .toStdString()
        );
    }
    require(api.lastUpdateBody.value(QStringLiteral("revision")).toInt() == 1,
            "metadata update must send current revision");
    auto* replaceButton = requiredChild<QPushButton>(
        widget, "bomReplaceItemsButton"
    );
    require(waitUntil([&]() {
        return replaceButton->isEnabled() &&
               requiredChild<QLabel>(widget, "bomRevisionLabel")
                   ->text().startsWith(QStringLiteral("2"));
    }), "metadata response must update revision before replacing items");

    bomItems->item(0, 3)->setText(QStringLiteral("2.250000"));
    replaceButton->click();
    require(waitUntil([&]() { return api.replaceCount == 1; }),
            "replace items action must call item endpoint");
    require(api.lastReplaceBody.value(QStringLiteral("revision")).toInt() == 2,
            "item replacement must send updated revision");
    require(api.lastReplaceBody.value(QStringLiteral("items")).toArray().first().toObject()
                .value(QStringLiteral("quantityMicros")).toInteger() == 2'250'000,
            "replacement quantity must convert exactly to micros");

    auto* toggleButton = requiredChild<QPushButton>(
        widget, "bomToggleEnabledButton"
    );
    require(waitUntil([&]() {
        return toggleButton->isEnabled() &&
               requiredChild<QLabel>(widget, "bomRevisionLabel")
                   ->text().startsWith(QStringLiteral("3"));
    }), "item response must update revision before toggling state");
    toggleButton->click();
    require(waitUntil([&]() { return api.toggleCount == 1; }),
            "toggle action must call enabled endpoint");
    require(api.lastToggleBody.value(QStringLiteral("revision")).toInt() == 3,
            "toggle must send latest revision");
    require(!api.lastToggleBody.value(QStringLiteral("isEnabled")).toBool(true),
            "enabled BOM must be disabled");
    require(waitUntil([&]() {
        return requiredChild<QPushButton>(widget, "bomSaveButton")->isEnabled() &&
               requiredChild<QLabel>(widget, "bomRevisionLabel")
                   ->text().startsWith(QStringLiteral("4"));
    }), "toggle response must update revision before the next operation");

    require(quoteCombo->count() == 1, "quote material picker must use backend materials");
    requiredChild<QPushButton>(widget, "quoteAddLineButton")->click();
    auto* quoteLines = requiredChild<QTableWidget>(widget, "quoteLinesTable");
    quoteLines->item(0, 1)->setText(QStringLiteral("2.500000"));
    quoteLines->item(0, 2)->setText(QStringLiteral("12.34"));
    requiredChild<QDoubleSpinBox>(widget, "quoteFreightSpin")->setValue(10.25);
    requiredChild<QDoubleSpinBox>(widget, "quoteOtherFeesSpin")->setValue(2.50);
    auto* markupSpin = requiredChild<QDoubleSpinBox>(widget, "quoteMarkupSpin");
    auto* taxSpin = requiredChild<QDoubleSpinBox>(widget, "quoteTaxSpin");
    require(markupSpin->minimum() == 0.0 && markupSpin->maximum() == 100.0,
            "markup must be restricted to 0-100 percent");
    require(taxSpin->minimum() == 0.0 && taxSpin->maximum() == 100.0,
            "tax must be restricted to 0-100 percent");
    markupSpin->setValue(20.50);
    taxSpin->setValue(13.00);
    requiredChild<QPushButton>(widget, "quoteCalculateButton")->click();
    require(waitUntil([&]() { return api.quoteCount == 1; }),
            "quote calculation must call calculate endpoint");
    const auto quoteLine = api.lastQuoteBody.value(QStringLiteral("lines"))
                               .toArray().first().toObject();
    require(quoteLine.value(QStringLiteral("quantityMicros")).toInteger() == 2'500'000,
            "quote quantity must convert exactly to micros");
    require(quoteLine.value(QStringLiteral("unitPriceCents")).toInteger() == 1234,
            "quote unit price must convert exactly to cents");
    require(api.lastQuoteBody.value(QStringLiteral("freightCents")).toInteger() == 1025,
            "freight must convert exactly to cents");
    require(api.lastQuoteBody.value(QStringLiteral("markupBasisPoints")).toInteger() == 2050,
            "markup percentage must convert exactly to basis points");
    require(api.lastQuoteBody.value(QStringLiteral("taxBasisPoints")).toInteger() == 1300,
            "tax percentage must convert exactly to basis points");
    require(waitUntil([&]() {
        return requiredChild<QLabel>(widget, "quotePriceWithTaxLabel")->text()
            == QStringLiteral("¥56.50");
    }), "calculation result must be displayed as currency");
    require(requiredChild<QLabel>(widget, "quoteScopeLabel")->text()
                .contains(QStringLiteral("不会保存")),
            "UI must state that quote history is not saved");

    api.failNextUpdate = true;
    requiredChild<QPushButton>(widget, "bomSaveButton")->click();
    auto* status = requiredChild<QLabel>(widget, "bomStatusLabel");
    require(waitUntil([&]() { return status->text().contains(QStringLiteral("刷新")); }),
            "revision conflict must be explained in Chinese");
}

void rolesEnforceReadOnlyAndCalculationRules(TestApi& api) {
    {
        ApiClient client(api.baseUrl());
        loginAs(client, api, QStringLiteral("quoter"));
        BomQuoteWidget widget(&client);
        widget.show();
        auto* combo = requiredChild<QComboBox>(widget, "quoteMaterialCombo");
        require(waitUntil([&]() { return combo->count() == 1; }),
                "quoter materials must load");
        require(!requiredChild<QPushButton>(widget, "bomNewButton")->isEnabled(),
                "quoter must not create BOMs");
        require(!requiredChild<QPushButton>(widget, "bomSaveButton")->isEnabled(),
                "quoter must not edit BOMs");
        require(requiredChild<QPushButton>(widget, "quoteAddLineButton")->isEnabled(),
                "quoter must be able to prepare a calculation");
    }
    {
        ApiClient client(api.baseUrl());
        loginAs(client, api, QStringLiteral("viewer"));
        BomQuoteWidget widget(&client);
        widget.show();
        auto* list = requiredChild<QTableWidget>(widget, "bomListTable");
        require(waitUntil([&]() { return list->rowCount() == 1; }),
                "viewer BOM list must load");
        list->selectRow(0);
        require(requiredChild<QPushButton>(widget, "bomViewButton")->isEnabled(),
                "viewer must be able to view BOM details");
        require(!requiredChild<QPushButton>(widget, "bomNewButton")->isEnabled(),
                "viewer must not create BOMs");
        require(!requiredChild<QPushButton>(widget, "quoteAddLineButton")->isEnabled(),
                "viewer must not prepare quote calculations");
        require(!requiredChild<QPushButton>(widget, "quoteCalculateButton")->isEnabled(),
                "viewer must not calculate quotes");
    }
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    try {
        TestApi api;
        const std::vector<std::pair<std::string, std::function<void()>>> tests{
            {
                "admin BOM lifecycle, quote contract, and Chinese errors",
                [&]() { adminCanManageBomAndCalculateQuote(api); },
            },
            {
                "quoter and viewer permissions",
                [&]() { rolesEnforceReadOnlyAndCalculationRules(api); },
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
