#include "manage/desktop/api_client.h"
#include "manage/desktop/catalog_widget.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QGroupBox>
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
#include <QNetworkAccessManager>
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
using manage::desktop::CatalogWidget;
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

template <typename Widget>
Widget* child(QWidget& parent, const char* objectName) {
    auto* result = parent.findChild<Widget*>(QString::fromLatin1(objectName));
    require(result != nullptr, std::string("missing widget: ") + objectName);
    return result;
}

bool waitUntil(const std::function<bool()>& condition, int timeoutMs = 5'000) {
    QElapsedTimer elapsed;
    elapsed.start();
    while (!condition() && elapsed.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(5);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    return condition();
}

ApiResponse waitForApi(const std::function<void(ApiClient::Callback)>& begin) {
    std::optional<ApiResponse> result;
    begin([&](ApiResponse response) { result = std::move(response); });
    require(waitUntil([&]() { return result.has_value(); }), "API callback timed out");
    return std::move(*result);
}

QJsonObject requestObject(const QHttpServerRequest& request) {
    return QJsonDocument::fromJson(request.body()).object();
}

QJsonObject materialObject() {
    return {
        {QStringLiteral("id"), 11},
        {QStringLiteral("code"), QStringLiteral("MAT-011")},
        {QStringLiteral("name"), QStringLiteral("测试钢板")},
        {QStringLiteral("specification"), QStringLiteral("2 mm")},
        {QStringLiteral("unit"), QStringLiteral("张")},
        {QStringLiteral("category"), QStringLiteral("金属")},
        {QStringLiteral("currentUnitPriceCents"), 1'234},
        {QStringLiteral("isEnabled"), true},
        {QStringLiteral("revision"), 7},
    };
}

QJsonObject customerObject() {
    return {
        {QStringLiteral("id"), 21},
        {QStringLiteral("name"), QStringLiteral("测试客户")},
        {QStringLiteral("contactName"), QStringLiteral("张三")},
        {QStringLiteral("phone"), QStringLiteral("13800000000")},
        {QStringLiteral("address"), QStringLiteral("测试地址")},
        {QStringLiteral("notes"), QStringLiteral("重点客户")},
        {QStringLiteral("revision"), 4},
    };
}

QJsonObject supplierObject() {
    return {
        {QStringLiteral("id"), 31},
        {QStringLiteral("materialId"), 11},
        {QStringLiteral("supplierName"), QStringLiteral("测试供应商")},
        {QStringLiteral("contactName"), QStringLiteral("李四")},
        {QStringLiteral("phone"), QStringLiteral("13900000000")},
        {QStringLiteral("leadDays"), 7},
        {QStringLiteral("isDefault"), false},
        {QStringLiteral("isEnabled"), true},
        {QStringLiteral("revision"), 3},
    };
}

QJsonObject priceObject() {
    return {
        {QStringLiteral("id"), 41},
        {QStringLiteral("supplierId"), 31},
        {QStringLiteral("copperPriceCents"), QJsonValue::Null},
        {QStringLiteral("unitPriceCents"), 1'100},
        {QStringLiteral("isDefault"), false},
        {QStringLiteral("isEnabled"), true},
        {QStringLiteral("revision"), 1},
    };
}

struct CatalogApi final {
    QHttpServer server;
    QTcpServer tcpServer;
    quint16 port{};
    int materialGets{};
    int customerGets{};
    int materialPosts{};
    int materialPuts{};
    int materialPatches{};
    int bundlePosts{};
    int bundlePuts{};
    int customerPosts{};
    int customerPuts{};
    bool forbidMaterialList{};
    bool conflictNextMaterialPut{};
    bool conflictNextBundlePut{};
    QUrlQuery lastMaterialQuery;
    QUrlQuery lastCustomerQuery;
    QJsonObject lastMaterialPost;
    QJsonObject lastMaterialPut;
    QJsonObject lastMaterialPatch;
    QJsonObject lastBundlePost;
    QJsonObject lastBundlePut;
    QJsonObject lastCustomerPost;
    QJsonObject lastCustomerPut;

    CatalogApi() {
        server.route(
            QStringLiteral("/api/v1/auth/login"),
            QHttpServerRequest::Method::Post,
            [](const QHttpServerRequest& request) {
                const auto username = requestObject(request)
                                          .value(QStringLiteral("username"))
                                          .toString();
                const auto role =
                    username == QStringLiteral("admin") ||
                            username == QStringLiteral("temporary-admin")
                                      ? QStringLiteral("admin")
                                      : username;
                return QHttpServerResponse(QJsonObject{
                    {QStringLiteral("accessToken"), QStringLiteral("catalog-test-token")},
                    {QStringLiteral("expiresAt"), QStringLiteral("2026-08-06T12:00:00Z")},
                    {
                        QStringLiteral("user"),
                        QJsonObject{
                            {QStringLiteral("username"), username},
                            {QStringLiteral("role"), role},
                            {
                                QStringLiteral("mustChangePassword"),
                                username == QStringLiteral("temporary-admin")
                            },
                        }
                    },
                });
            }
        );
        server.route(
            QStringLiteral("/api/v1/auth/change-password"),
            QHttpServerRequest::Method::Post,
            []() {
                return QHttpServerResponse(QJsonObject{
                    {QStringLiteral("status"), QStringLiteral("password_changed")},
                    {
                        QStringLiteral("user"),
                        QJsonObject{
                            {QStringLiteral("username"), QStringLiteral("admin")},
                            {QStringLiteral("role"), QStringLiteral("admin")},
                            {QStringLiteral("mustChangePassword"), false},
                        }
                    },
                });
            }
        );
        server.route(
            QStringLiteral("/api/v1/materials"),
            QHttpServerRequest::Method::Get,
            [this](const QHttpServerRequest& request) {
                ++materialGets;
                lastMaterialQuery = request.query();
                if (forbidMaterialList) {
                    return QHttpServerResponse(
                        QJsonObject{
                            {QStringLiteral("error"), QStringLiteral("forbidden")},
                            {QStringLiteral("message"), QStringLiteral("not allowed")},
                        },
                        StatusCode::Forbidden
                    );
                }
                return QHttpServerResponse(QJsonObject{
                    {QStringLiteral("items"), QJsonArray{materialObject()}},
                    {QStringLiteral("page"), lastMaterialQuery.queryItemValue(QStringLiteral("page")).toInt()},
                    {QStringLiteral("pageSize"), 20},
                    {QStringLiteral("total"), 21},
                    {QStringLiteral("totalPages"), 2},
                });
            }
        );
        server.route(
            QStringLiteral("/api/v1/customers"),
            QHttpServerRequest::Method::Get,
            [this](const QHttpServerRequest& request) {
                ++customerGets;
                lastCustomerQuery = request.query();
                return QHttpServerResponse(QJsonObject{
                    {QStringLiteral("items"), QJsonArray{customerObject()}},
                    {QStringLiteral("page"), lastCustomerQuery.queryItemValue(QStringLiteral("page")).toInt()},
                    {QStringLiteral("pageSize"), 20},
                    {QStringLiteral("total"), 21},
                    {QStringLiteral("totalPages"), 2},
                });
            }
        );
        server.route(
            QStringLiteral("/api/v1/materials"),
            QHttpServerRequest::Method::Post,
            [this](const QHttpServerRequest& request) {
                ++materialPosts;
                lastMaterialPost = requestObject(request);
                auto response = materialObject();
                response.insert(QStringLiteral("revision"), 1);
                return QHttpServerResponse(response, StatusCode::Created);
            }
        );
        // 三级向导整包提交：物料 + 供应商 + 价格。
        server.route(
            QStringLiteral("/api/v1/materials/bundle"),
            QHttpServerRequest::Method::Post,
            [this](const QHttpServerRequest& request) {
                ++bundlePosts;
                lastBundlePost = requestObject(request);
                auto response = materialObject();
                response.insert(QStringLiteral("revision"), 1);
                return QHttpServerResponse(response, StatusCode::Created);
            }
        );
        // 三级向导整包替换（编辑模式）。
        server.route(
            QStringLiteral("/api/v1/materials/bundle"),
            QHttpServerRequest::Method::Put,
            [this](const QHttpServerRequest& request) {
                ++bundlePuts;
                lastBundlePut = requestObject(request);
                if (conflictNextBundlePut) {
                    conflictNextBundlePut = false;
                    return QHttpServerResponse(
                        QJsonObject{
                            {QStringLiteral("error"), QStringLiteral("revision_conflict")},
                            {QStringLiteral("message"), QStringLiteral("stale revision")},
                        },
                        StatusCode::Conflict
                    );
                }
                auto response = materialObject();
                response.insert(QStringLiteral("revision"), 8);
                return QHttpServerResponse(response);
            }
        );
        // 编辑向导：加载该物料现有供应商与价格作为起点。
        server.route(
            QStringLiteral("/api/v1/materials/<arg>/suppliers"),
            QHttpServerRequest::Method::Get,
            [this](qint64, const QHttpServerRequest&) {
                return QHttpServerResponse(QJsonObject{
                    {QStringLiteral("items"), QJsonArray{supplierObject()}},
                    {QStringLiteral("page"), 1},
                    {QStringLiteral("pageSize"), 20},
                    {QStringLiteral("total"), 1},
                    {QStringLiteral("totalPages"), 1},
                });
            }
        );
        server.route(
            QStringLiteral("/api/v1/suppliers/<arg>/prices"),
            QHttpServerRequest::Method::Get,
            [this](qint64, const QHttpServerRequest&) {
                return QHttpServerResponse(QJsonObject{
                    {QStringLiteral("items"), QJsonArray{priceObject()}},
                    {QStringLiteral("page"), 1},
                    {QStringLiteral("pageSize"), 20},
                    {QStringLiteral("total"), 1},
                    {QStringLiteral("totalPages"), 1},
                });
            }
        );
        server.route(
            QStringLiteral("/api/v1/materials/<arg>"),
            QHttpServerRequest::Method::Put,
            [this](qint64, const QHttpServerRequest& request) {
                ++materialPuts;
                lastMaterialPut = requestObject(request);
                if (conflictNextMaterialPut) {
                    conflictNextMaterialPut = false;
                    return QHttpServerResponse(
                        QJsonObject{
                            {QStringLiteral("error"), QStringLiteral("revision_conflict")},
                            {QStringLiteral("message"), QStringLiteral("stale revision")},
                        },
                        StatusCode::Conflict
                    );
                }
                auto response = materialObject();
                response.insert(QStringLiteral("revision"), 8);
                return QHttpServerResponse(response);
            }
        );
        server.route(
            QStringLiteral("/api/v1/materials/<arg>/enabled"),
            QHttpServerRequest::Method::Patch,
            [this](qint64, const QHttpServerRequest& request) {
                ++materialPatches;
                lastMaterialPatch = requestObject(request);
                auto response = materialObject();
                response.insert(QStringLiteral("isEnabled"), false);
                response.insert(QStringLiteral("revision"), 8);
                return QHttpServerResponse(response);
            }
        );
        server.route(
            QStringLiteral("/api/v1/customers"),
            QHttpServerRequest::Method::Post,
            [this](const QHttpServerRequest& request) {
                ++customerPosts;
                lastCustomerPost = requestObject(request);
                auto response = customerObject();
                response.insert(QStringLiteral("revision"), 1);
                return QHttpServerResponse(response, StatusCode::Created);
            }
        );
        server.route(
            QStringLiteral("/api/v1/customers/<arg>"),
            QHttpServerRequest::Method::Put,
            [this](qint64, const QHttpServerRequest& request) {
                ++customerPuts;
                lastCustomerPut = requestObject(request);
                auto response = customerObject();
                response.insert(QStringLiteral("revision"), 5);
                return QHttpServerResponse(response);
            }
        );

        if (!tcpServer.listen(QHostAddress::LocalHost, 0) ||
            !server.bind(&tcpServer)) {
            throw TestFailure("unable to start catalog UI test server");
        }
        port = tcpServer.serverPort();
    }

    QUrl baseUrl() const {
        return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(port));
    }
};

void login(ApiClient& client, const QString& username) {
    const auto response = waitForApi([&](ApiClient::Callback callback) {
        client.login(username, QStringLiteral("test password"), std::move(callback));
    });
    require(response.succeeded(), "test login must succeed");
}

void waitForInitialLists(CatalogWidget& widget) {
    auto* materials = child<QTableWidget>(widget, "materialsTable");
    auto* customers = child<QTableWidget>(widget, "customersTable");
    require(
        waitUntil([&]() { return materials->rowCount() == 1 && customers->rowCount() == 1; }),
        "catalog widget did not asynchronously load both lists"
    );
}

void adminCanSearchPageAndMutate(CatalogApi& api) {
    QNetworkAccessManager network;
    ApiClient client(api.baseUrl(), &network);
    login(client, QStringLiteral("admin"));
    CatalogWidget widget(&client);
    widget.show();
    waitForInitialLists(widget);

    auto* materialAdd = child<QPushButton>(widget, "materialAddButton");
    auto* materialEdit = child<QPushButton>(widget, "materialEditButton");
    auto* materialToggle = child<QPushButton>(widget, "materialToggleButton");
    auto* customerAdd = child<QPushButton>(widget, "customerAddButton");
    auto* materialsNext = child<QPushButton>(widget, "materialsNextButton");
    auto* customersNext = child<QPushButton>(widget, "customersNextButton");
    require(materialAdd->isEnabled(), "admin material add must be enabled");
    require(customerAdd->isEnabled(), "admin customer add must be enabled");

    const auto searchGets = api.materialGets;
    auto* materialSearch = child<QLineEdit>(widget, "materialsSearchEdit");
    materialSearch->setText(QStringLiteral("不锈钢 板"));
    child<QPushButton>(widget, "materialsSearchButton")->click();
    require(waitUntil([&]() {
        return api.materialGets > searchGets && materialsNext->isEnabled();
    }), "material search response must enable the next page");
    require(
        api.lastMaterialQuery.queryItemValue(QStringLiteral("search")) ==
            QStringLiteral("不锈钢 板"),
        "material search query must preserve UTF-8 text"
    );
    require(
        api.lastMaterialQuery.queryItemValue(QStringLiteral("page")) == QStringLiteral("1"),
        "search must reset page to one"
    );

    const auto pageGets = api.materialGets;
    materialsNext->click();
    require(waitUntil([&]() {
        return api.materialGets > pageGets &&
            child<QLabel>(widget, "materialsPageLabel")
                ->text().contains(QStringLiteral("第 2 / 2 页"));
    }), "next-page response missing");
    require(
        api.lastMaterialQuery.queryItemValue(QStringLiteral("page")) == QStringLiteral("2"),
        "next page must request page two"
    );
    const auto customerSearchGets = api.customerGets;
    child<QLineEdit>(widget, "customersSearchEdit")->setText(QStringLiteral("示例 公司"));
    child<QPushButton>(widget, "customersSearchButton")->click();
    require(waitUntil([&]() {
        return api.customerGets > customerSearchGets && customersNext->isEnabled();
    }), "customer search response must enable the next page");
    require(
        api.lastCustomerQuery.queryItemValue(QStringLiteral("search")) ==
            QStringLiteral("示例 公司"),
        "customer search query must preserve UTF-8 text"
    );
    const auto customerPageGets = api.customerGets;
    customersNext->click();
    require(waitUntil([&]() {
        return api.customerGets > customerPageGets &&
            child<QPushButton>(widget, "customersRefreshButton")->isEnabled();
    }), "customer next-page response missing");
    require(
        api.lastCustomerQuery.queryItemValue(QStringLiteral("page")) == QStringLiteral("2"),
        "customer next page must request page two"
    );

    materialAdd->click();
    // 三级向导第 1 步：物料信息（暂存，不落库）。
    child<QLineEdit>(widget, "materialCodeEdit")->setText(QStringLiteral("MAT-NEW"));
    child<QLineEdit>(widget, "materialNameEdit")->setText(QStringLiteral("新物料"));
    child<QLineEdit>(widget, "materialUnitEdit")->setText(QStringLiteral("件"));
    child<QLineEdit>(widget, "materialPriceEdit")->setText(QStringLiteral("12.34"));
    // 电线类物料：第三级允许同一供应商按铜价区分多条价格。
    child<QCheckBox>(widget, "materialCopperCheck")->setChecked(true);
    child<QPushButton>(widget, "materialSaveButton")->click();
    require(api.materialPosts == 0, "wizard step 1 must not post a material");
    require(
        child<QGroupBox>(widget, "materialBaseGroup")->isEnabled() == false &&
            child<QGroupBox>(widget, "supplierGroupBox")->isEnabled(),
        "wizard must lock material info and enable suppliers"
    );

    // 三级向导第 2 步：供应商（暂存）。
    child<QPushButton>(widget, "supplierAddButton")->click();
    child<QLineEdit>(widget, "supplierNameEdit")->setText(QStringLiteral("供应商甲"));
    child<QLineEdit>(widget, "supplierLeadDaysEdit")->setText(QStringLiteral("7"));
    child<QPushButton>(widget, "supplierSaveButton")->click();
    child<QPushButton>(widget, "supplierAddButton")->click();
    child<QLineEdit>(widget, "supplierNameEdit")->setText(QStringLiteral("供应商乙"));
    child<QLineEdit>(widget, "supplierLeadDaysEdit")->setText(QStringLiteral("10"));
    child<QPushButton>(widget, "supplierSaveButton")->click();
    child<QPushButton>(widget, "supplierConfirmButton")->click();

    // 三级向导第 3 步：价格（按供应商 + 铜价）。
    require(waitUntil([&]() {
        return child<QComboBox>(widget, "priceSupplierCombo")->count() == 2;
    }), "wizard price combo must list both suppliers");
    child<QTableWidget>(widget, "suppliersTable")->selectRow(0);
    child<QPushButton>(widget, "priceAddButton")->click();
    child<QLineEdit>(widget, "priceCopperEdit")->setText(QStringLiteral("7.00"));
    child<QLineEdit>(widget, "priceUnitEdit")->setText(QStringLiteral("11.00"));
    child<QPushButton>(widget, "priceSaveButton")->click();
    child<QPushButton>(widget, "priceAddButton")->click();
    child<QLineEdit>(widget, "priceCopperEdit")->setText(QStringLiteral("7.20"));
    child<QLineEdit>(widget, "priceUnitEdit")->setText(QStringLiteral("12.00"));
    child<QPushButton>(widget, "priceSaveButton")->click();

    // 整包提交：一次写入物料 + 供应商 + 价格。
    child<QPushButton>(widget, "bundleCommitButton")->click();
    require(waitUntil([&]() { return api.bundlePosts == 1; }), "bundle POST missing");
    require(
        api.lastBundlePost.value(QStringLiteral("material"))
            .toObject()
            .value(QStringLiteral("currentUnitPriceCents"))
            .toInteger() == 1'234,
        "bundle material carries yuan-to-cents conversion"
    );
    const auto bundleSuppliers =
        api.lastBundlePost.value(QStringLiteral("suppliers")).toArray();
    require(bundleSuppliers.size() == 2, "bundle posts both suppliers");
    require(
        bundleSuppliers.at(0).toObject().value(QStringLiteral("supplier"))
                .toObject().value(QStringLiteral("supplierName")).toString() ==
            QStringLiteral("供应商甲"),
        "bundle keeps first supplier"
    );
    require(
        bundleSuppliers.at(1).toObject().value(QStringLiteral("supplier"))
                .toObject().value(QStringLiteral("leadDays")).toInt() == 10,
        "bundle keeps second supplier lead days"
    );
    const auto firstPrices = bundleSuppliers.at(0).toObject()
                                 .value(QStringLiteral("prices")).toArray();
    require(firstPrices.size() == 2, "first supplier keeps two prices");
    require(
        firstPrices.at(1).toObject().value(QStringLiteral("copperPriceCents"))
                .toInteger() == 720,
        "second price of first supplier keeps copper price cents"
    );

    require(waitUntil([&]() { return child<QPushButton>(widget, "materialsRefreshButton")->isEnabled(); }),
            "material refresh after create must finish");
    auto* materials = child<QTableWidget>(widget, "materialsTable");
    materials->selectRow(0);
    require(waitUntil([&]() { return materialEdit->isEnabled(); }), "material selection must enable edit");
    materialEdit->click();
    // 编辑模式同样走三级向导：修改物料信息 → 确认 → 确定供应商 → 整包替换提交。
    require(waitUntil([&]() { return child<QGroupBox>(widget, "materialBaseGroup")->isEnabled(); }),
            "edit wizard must enable step one");
    require(waitUntil([&]() {
        return child<QTableWidget>(widget, "suppliersTable")->rowCount() == 1;
    }), "edit wizard must load the existing supplier");
    child<QLineEdit>(widget, "materialPriceEdit")->setText(QStringLiteral("99.05"));
    child<QPushButton>(widget, "materialSaveButton")->click();
    require(waitUntil([&]() {
        return !child<QGroupBox>(widget, "materialBaseGroup")->isEnabled() &&
               child<QGroupBox>(widget, "supplierGroupBox")->isEnabled();
    }), "edit wizard must lock step one and enable suppliers");
    // 编辑模式预加载了现有供应商，直接确定进入价格。
    child<QPushButton>(widget, "supplierConfirmButton")->click();
    require(waitUntil([&]() {
        return child<QComboBox>(widget, "priceSupplierCombo")->count() == 1;
    }), "edit wizard must list the existing supplier");
    child<QPushButton>(widget, "bundleCommitButton")->click();
    require(waitUntil([&]() { return api.bundlePuts == 1; }), "bundle PUT missing");
    require(
        api.lastBundlePut.value(QStringLiteral("materialId")).toInteger() == 11,
        "bundle PUT must carry the material id"
    );
    require(
        api.lastBundlePut.value(QStringLiteral("materialRevision")).toInteger() == 7,
        "bundle PUT must carry the loaded revision"
    );
    require(
        api.lastBundlePut.value(QStringLiteral("material"))
            .toObject()
            .value(QStringLiteral("currentUnitPriceCents"))
            .toInteger() == 9'905,
        "edited yuan input must be converted to cents in bundle"
    );
    const auto editSuppliers =
        api.lastBundlePut.value(QStringLiteral("suppliers")).toArray();
    require(editSuppliers.size() == 1, "bundle PUT keeps the existing supplier");
    require(
        editSuppliers.at(0).toObject().value(QStringLiteral("supplier"))
                .toObject().value(QStringLiteral("id")).toInteger() == 31,
        "bundle PUT keeps supplier identity for optimistic lock"
    );

    require(waitUntil([&]() { return child<QPushButton>(widget, "materialsRefreshButton")->isEnabled(); }),
            "material refresh after update must finish");
    materials->selectRow(0);
    materialToggle->click();
    require(waitUntil([&]() { return api.materialPatches == 1; }), "material PATCH missing");
    require(
        api.lastMaterialPatch.value(QStringLiteral("revision")).toInteger() == 7 &&
            !api.lastMaterialPatch.value(QStringLiteral("isEnabled")).toBool(true),
        "enable toggle must send revision and opposite state"
    );
    require(
        waitUntil([&]() {
            return child<QPushButton>(widget, "materialsRefreshButton")->isEnabled();
        }),
        "material refresh after enable toggle must finish"
    );

    materialAdd->click();
    child<QLineEdit>(widget, "materialCodeEdit")->setText(QStringLiteral("MAT-BAD"));
    child<QLineEdit>(widget, "materialNameEdit")->setText(QStringLiteral("错误金额"));
    child<QLineEdit>(widget, "materialUnitEdit")->setText(QStringLiteral("件"));
    child<QLineEdit>(widget, "materialPriceEdit")->setText(QStringLiteral("12.345"));
    const auto postsBeforeInvalid = api.materialPosts;
    child<QPushButton>(widget, "materialSaveButton")->click();
    QCoreApplication::processEvents();
    require(api.materialPosts == postsBeforeInvalid, "invalid price must not reach API");
    require(
        child<QLabel>(widget, "materialsStatusLabel")->text().contains(QStringLiteral("两位小数")),
        "invalid price must show a Chinese validation message"
    );

    customerAdd->click();
    child<QLineEdit>(widget, "customerNameEdit")->setText(QStringLiteral("新客户"));
    child<QLineEdit>(widget, "customerContactEdit")->setText(QStringLiteral("李四"));
    child<QPushButton>(widget, "customerSaveButton")->click();
    require(waitUntil([&]() { return api.customerPosts == 1; }), "customer POST missing");
    require(
        !api.lastCustomerPost.contains(QStringLiteral("revision")),
        "new customer must not send revision"
    );

    require(waitUntil([&]() { return child<QPushButton>(widget, "customersRefreshButton")->isEnabled(); }),
            "customer refresh after create must finish");
    auto* customers = child<QTableWidget>(widget, "customersTable");
    customers->selectRow(0);
    auto* customerEdit = child<QPushButton>(widget, "customerEditButton");
    require(waitUntil([&]() { return customerEdit->isEnabled(); }), "customer selection must enable edit");
    customerEdit->click();
    child<QLineEdit>(widget, "customerPhoneEdit")->setText(QStringLiteral("13900000000"));
    child<QPushButton>(widget, "customerSaveButton")->click();
    require(waitUntil([&]() { return api.customerPuts == 1; }), "customer PUT missing");
    require(
        api.lastCustomerPut.value(QStringLiteral("revision")).toInteger() == 4,
        "customer edit must carry the loaded revision"
    );
}

void temporaryPasswordBlocksCatalogUntilChanged(CatalogApi& api) {
    QNetworkAccessManager network;
    ApiClient client(api.baseUrl(), &network);
    login(client, QStringLiteral("temporary-admin"));
    const auto materialGets = api.materialGets;
    const auto customerGets = api.customerGets;

    CatalogWidget widget(&client);
    QElapsedTimer settle;
    settle.start();
    while (settle.elapsed() < 200) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(2);
    }
    require(
        api.materialGets == materialGets && api.customerGets == customerGets,
        "temporary-password session must not request catalog APIs"
    );
    require(
        child<QLabel>(widget, "materialsStatusLabel")
            ->text()
            .contains(QStringLiteral("请先修改临时密码")),
        "temporary-password session must show a clear Chinese prompt"
    );
    require(
        !child<QPushButton>(widget, "materialAddButton")->isEnabled() &&
            !child<QPushButton>(widget, "customerAddButton")->isEnabled(),
        "temporary-password admin must not have write controls"
    );
    require(
        !child<QPushButton>(widget, "materialsRefreshButton")->isEnabled(),
        "temporary-password session must not enable catalog loading controls"
    );

    const auto changed = waitForApi([&](ApiClient::Callback callback) {
        client.changePassword(
            QStringLiteral("temporary password"),
            QStringLiteral("permanent password"),
            std::move(callback)
        );
    });
    require(changed.succeeded(), "test password change must succeed");
    require(
        !client.session()
             .user
             .value(QStringLiteral("mustChangePassword"))
             .toBool(true),
        "password change must update the in-memory user"
    );

    require(
        waitUntil([&]() {
            return api.materialGets > materialGets && api.customerGets > customerGets;
        }),
        "password change must automatically load both catalog lists"
    );
    require(
        waitUntil([&]() {
            return child<QPushButton>(widget, "materialAddButton")->isEnabled() &&
                child<QPushButton>(widget, "customerAddButton")->isEnabled();
        }),
        "admin write controls must recover after password change"
    );
}

void errorsAreShownInChinese(CatalogApi& api) {
    QNetworkAccessManager network;
    ApiClient client(api.baseUrl(), &network);
    login(client, QStringLiteral("admin"));
    CatalogWidget widget(&client);
    waitForInitialLists(widget);

    auto* table = child<QTableWidget>(widget, "materialsTable");
    table->selectRow(0);
    auto* editButton = child<QPushButton>(widget, "materialEditButton");
    require(waitUntil([&]() { return editButton->isEnabled(); }),
            "material selection must enable conflict edit");
    editButton->click();
    // 编辑向导：等待现有供应商加载完成（异步）后进入第 1 步确认。
    require(waitUntil([&]() {
        return child<QTableWidget>(widget, "suppliersTable")->rowCount() == 1;
    }), "edit wizard must load the existing supplier");
    child<QPushButton>(widget, "materialSaveButton")->click();
    require(waitUntil([&]() {
        return !child<QGroupBox>(widget, "materialBaseGroup")->isEnabled() &&
               child<QGroupBox>(widget, "supplierGroupBox")->isEnabled();
    }), "conflict wizard must advance to suppliers");
    api.conflictNextBundlePut = true;
    const auto putsBeforeConflict = api.bundlePuts;
    child<QPushButton>(widget, "supplierConfirmButton")->click();
    require(waitUntil([&]() {
        return child<QPushButton>(widget, "bundleCommitButton")->isEnabled();
    }), "conflict wizard must advance to price step");
    child<QPushButton>(widget, "bundleCommitButton")->click();
    require(waitUntil([&]() { return api.bundlePuts > putsBeforeConflict; }),
            "conflict bundle PUT missing");
    require(
        waitUntil([&]() {
            return child<QLabel>(widget, "materialsStatusLabel")
                ->text()
                .contains(QStringLiteral("数据已被其他操作修改"));
        }),
        "revision conflict must be explained in Chinese"
    );

    api.forbidMaterialList = true;
    const auto gets = api.materialGets;
    child<QPushButton>(widget, "materialsRefreshButton")->click();
    require(waitUntil([&]() { return api.materialGets > gets; }), "forbidden refresh missing");
    require(
        waitUntil([&]() {
            return child<QLabel>(widget, "materialsStatusLabel")
                ->text()
                .contains(QStringLiteral("没有执行此操作的权限"));
        }),
        "HTTP 403 must be explained in Chinese"
    );
    api.forbidMaterialList = false;
}

void quoterAndViewerAreReadOnly(CatalogApi& api) {
    for (const auto& role : {QStringLiteral("quoter"), QStringLiteral("viewer")}) {
        QNetworkAccessManager network;
        ApiClient client(api.baseUrl(), &network);
        login(client, role);
        CatalogWidget widget(&client);
        waitForInitialLists(widget);

        child<QTableWidget>(widget, "materialsTable")->selectRow(0);
        child<QTableWidget>(widget, "customersTable")->selectRow(0);
        QCoreApplication::processEvents();
        require(!child<QPushButton>(widget, "materialAddButton")->isEnabled(),
                "non-admin material add must be disabled");
        require(!child<QPushButton>(widget, "materialEditButton")->isEnabled(),
                "non-admin material edit must be disabled");
        require(!child<QPushButton>(widget, "materialToggleButton")->isEnabled(),
                "non-admin material toggle must be disabled");
        require(!child<QPushButton>(widget, "customerAddButton")->isEnabled(),
                "non-admin customer add must be disabled");
        require(!child<QPushButton>(widget, "customerEditButton")->isEnabled(),
                "non-admin customer edit must be disabled");
        require(child<QPushButton>(widget, "materialsRefreshButton")->isEnabled(),
                "read-only user must still be able to refresh");

        const auto buttons = widget.findChildren<QPushButton*>();
        for (const auto* candidate : buttons) {
            require(!candidate->text().contains(QStringLiteral("删除")),
                    "catalog widget must not expose a delete button");
        }
    }
}

void networkFailureHasChineseMessage() {
    QHttpServer loginServer;
    QTcpServer tcpServer;
    loginServer.route(
        QStringLiteral("/api/v1/auth/login"),
        QHttpServerRequest::Method::Post,
        []() {
            return QHttpServerResponse(QJsonObject{
                {QStringLiteral("accessToken"), QStringLiteral("network-test-token")},
                {
                    QStringLiteral("user"),
                    QJsonObject{
                        {QStringLiteral("role"), QStringLiteral("admin")},
                        {QStringLiteral("mustChangePassword"), false},
                    }
                },
            });
        }
    );
    require(tcpServer.listen(QHostAddress::LocalHost, 0), "unable to start login server");
    require(loginServer.bind(&tcpServer), "unable to bind login server");
    const auto port = tcpServer.serverPort();

    QNetworkAccessManager network;
    ApiClient client(
        QUrl(QStringLiteral("http://127.0.0.1:%1").arg(port)),
        &network
    );
    login(client, QStringLiteral("admin"));
    network.clearConnectionCache();
    tcpServer.close();
    CatalogWidget widget(&client);
    require(
        waitUntil([&]() {
            return child<QLabel>(widget, "materialsStatusLabel")
                ->text()
                .contains(QStringLiteral("网络连接失败"));
        }),
        "network failure must be explained in Chinese"
    );
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    try {
        CatalogApi api;
        const std::vector<std::pair<std::string, std::function<void()>>> tests = {
            {
                "temporary password gate",
                [&]() { temporaryPasswordBlocksCatalogUntilChanged(api); }
            },
            {"admin catalog workflow", [&]() { adminCanSearchPageAndMutate(api); }},
            {"Chinese error messages", [&]() { errorsAreShownInChinese(api); }},
            {"read-only roles", [&]() { quoterAndViewerAreReadOnly(api); }},
            {"network failure message", []() { networkFailureHasChineseMessage(); }},
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
        std::cerr << "[FAIL] setup: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
