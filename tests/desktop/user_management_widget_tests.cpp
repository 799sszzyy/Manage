#include "manage/desktop/api_client.h"
#include "manage/desktop/user_management_widget.h"

#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponse>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTcpServer>
#include <QThread>

#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>

namespace {

class TestFailure final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
void require(bool condition, const char* message) {
    if (!condition) throw TestFailure(message);
}
bool waitUntil(const std::function<bool()>& predicate) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < 5'000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(2);
    }
    return predicate();
}
template<typename T> T* child(QObject& object, const char* name) {
    auto* result = object.findChild<T*>(QString::fromLatin1(name));
    require(result != nullptr, name);
    return result;
}

struct TestApi final {
    QHttpServer server;
    QTcpServer tcp;
    int lists{};
    int creates{};
    QJsonObject createdBody;

    TestApi() {
        server.route(QStringLiteral("/api/v1/auth/login"), QHttpServerRequest::Method::Post,
            [](const QHttpServerRequest&) {
                return QHttpServerResponse(QJsonObject{
                    {QStringLiteral("accessToken"), QStringLiteral("user-token")},
                    {QStringLiteral("expiresAt"), QStringLiteral("2026-08-06T00:00:00Z")},
                    {QStringLiteral("user"), QJsonObject{
                        {QStringLiteral("id"), 1}, {QStringLiteral("username"), QStringLiteral("admin")},
                        {QStringLiteral("role"), QStringLiteral("admin")},
                        {QStringLiteral("mustChangePassword"), false},
                    }},
                });
            });
        server.route(QStringLiteral("/api/v1/users"), QHttpServerRequest::Method::Get,
            [this](const QHttpServerRequest&) {
                ++lists;
                return QHttpServerResponse(QJsonObject{
                    {QStringLiteral("items"), QJsonArray{QJsonObject{
                        {QStringLiteral("id"), 2}, {QStringLiteral("username"), QStringLiteral("quote.user")},
                        {QStringLiteral("displayName"), QStringLiteral("报价员甲")},
                        {QStringLiteral("role"), QStringLiteral("quoter")},
                        {QStringLiteral("enabled"), true}, {QStringLiteral("mustChangePassword"), true},
                        {QStringLiteral("revision"), 1},
                    }}},
                    {QStringLiteral("total"), 1}, {QStringLiteral("page"), 1},
                    {QStringLiteral("pageSize"), 20},
                });
            });
        server.route(QStringLiteral("/api/v1/users"), QHttpServerRequest::Method::Post,
            [this](const QHttpServerRequest& request) {
                ++creates;
                createdBody = QJsonDocument::fromJson(request.body()).object();
                return QHttpServerResponse(QJsonObject{{QStringLiteral("user"), QJsonObject{
                    {QStringLiteral("id"), 3}, {QStringLiteral("username"), createdBody.value(QStringLiteral("username"))},
                    {QStringLiteral("displayName"), createdBody.value(QStringLiteral("displayName"))},
                    {QStringLiteral("role"), createdBody.value(QStringLiteral("role"))},
                    {QStringLiteral("enabled"), true}, {QStringLiteral("mustChangePassword"), true},
                    {QStringLiteral("revision"), 1},
                }}}, QHttpServerResponder::StatusCode::Created);
            });
        require(tcp.listen(QHostAddress::LocalHost, 0) && server.bind(&tcp),
                "start test API");
    }
    QUrl url() const {
        return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(tcp.serverPort()));
    }
};

void widgetLoadsAndCreatesThroughApi() {
    TestApi api;
    manage::desktop::ApiClient client(api.url());
    std::optional<manage::desktop::ApiResponse> login;
    client.login(QStringLiteral("admin"), QStringLiteral("password"),
                 [&](auto response) { login = std::move(response); });
    require(waitUntil([&] { return login.has_value(); }) && login->succeeded(),
            "admin login");

    manage::desktop::UserManagementWidget widget(&client);
    widget.show();
    auto* table = child<QTableWidget>(widget, "userTable");
    require(waitUntil([&] { return api.lists == 1 && table->rowCount() == 1; }),
            "user list displayed");
    require(table->item(0, 0)->text() == QStringLiteral("quote.user"),
            "username displayed");

    child<QLineEdit>(widget, "usernameEdit")->setText(QStringLiteral("viewer.one"));
    child<QLineEdit>(widget, "userDisplayNameEdit")->setText(QStringLiteral("查看员一"));
    child<QComboBox>(widget, "userRoleCombo")->setCurrentIndex(2);
    child<QLineEdit>(widget, "temporaryPasswordEdit")->setText(QStringLiteral("Temporary Password 3"));
    child<QPushButton>(widget, "saveUserButton")->click();
    require(waitUntil([&] { return api.creates == 1 && api.lists >= 2; }),
            "create request and refresh complete");
    require(api.createdBody.value(QStringLiteral("username")).toString() == QStringLiteral("viewer.one") &&
            api.createdBody.value(QStringLiteral("role")).toString() == QStringLiteral("viewer"),
            "form payload sent to API");
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    try {
        widgetLoadsAndCreatesThroughApi();
        std::cout << "1/1 user widget tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
