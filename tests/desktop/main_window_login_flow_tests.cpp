#include "manage/desktop/main_window.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponder>
#include <QHttpServerResponse>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTcpServer>
#include <QUrl>

#include <cstdlib>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

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
Widget* requiredWidget(QWidget& parent, const char* objectName) {
    auto* widget = parent.findChild<Widget*>(QString::fromLatin1(objectName));
    require(widget != nullptr, std::string("missing widget: ") + objectName);
    return widget;
}

void waitUntil(
    const std::function<bool()>& condition,
    const std::string& failureMessage,
    int timeoutMilliseconds = 5'000
) {
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < timeoutMilliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    require(condition(), failureMessage);
}

QJsonObject requestObject(const QHttpServerRequest& request) {
    return QJsonDocument::fromJson(request.body()).object();
}

struct DesktopAuthApi final {
    enum class MeMode { Success, Forbidden, Unauthorized };

    QHttpServer server;
    QTcpServer tcpServer;
    quint16 port{};
    MeMode meMode{MeMode::Success};
    bool bootstrapped{};
    bool passwordChanged{};
    int logoutCount{};
    QByteArray lastAuthorization;
    QJsonObject lastBootstrapBody;
    QJsonObject lastChangePasswordBody;

    DesktopAuthApi() {
        server.route(
            QStringLiteral("/api/v1/auth/bootstrap"),
            QHttpServerRequest::Method::Post,
            [this](const QHttpServerRequest& request) {
                lastBootstrapBody = requestObject(request);
                bootstrapped = true;
                return QHttpServerResponse(
                    QJsonObject{{
                        QStringLiteral("user"),
                        userObject(true)
                    }},
                    StatusCode::Created
                );
            }
        );
        server.route(
            QStringLiteral("/api/v1/auth/login"),
            QHttpServerRequest::Method::Post,
            [this](const QHttpServerRequest& request) {
                const auto body = requestObject(request);
                const auto password = body.value(QStringLiteral("password")).toString();
                const auto accepted = bootstrapped &&
                    ((!passwordChanged &&
                      password == QStringLiteral("Temporary Password 1")) ||
                     (passwordChanged &&
                      password == QStringLiteral("Permanent Password 2")));
                if (!accepted) {
                    return QHttpServerResponse(
                        QJsonObject{
                            {
                                QStringLiteral("error"),
                                QStringLiteral("invalid_credentials")
                            },
                            {
                                QStringLiteral("message"),
                                QStringLiteral("credentials rejected")
                            },
                        },
                        StatusCode::Unauthorized
                    );
                }
                return QHttpServerResponse(QJsonObject{
                    {
                        QStringLiteral("accessToken"),
                        QStringLiteral("desktop-flow-token")
                    },
                    {
                        QStringLiteral("expiresAt"),
                        QStringLiteral("2026-08-06T12:00:00Z")
                    },
                    {
                        QStringLiteral("user"),
                        userObject(!passwordChanged)
                    },
                });
            }
        );
        server.route(
            QStringLiteral("/api/v1/auth/change-password"),
            QHttpServerRequest::Method::Post,
            [this](const QHttpServerRequest& request) {
                lastAuthorization = request.value("Authorization");
                lastChangePasswordBody = requestObject(request);
                passwordChanged = true;
                return QHttpServerResponse(QJsonObject{
                    {
                        QStringLiteral("status"),
                        QStringLiteral("password_changed")
                    },
                    {QStringLiteral("user"), userObject(false)},
                });
            }
        );
        server.route(
            QStringLiteral("/api/v1/auth/me"),
            QHttpServerRequest::Method::Get,
            [this](const QHttpServerRequest& request) {
                lastAuthorization = request.value("Authorization");
                if (meMode == MeMode::Forbidden) {
                    return QHttpServerResponse(
                        QJsonObject{
                            {QStringLiteral("error"), QStringLiteral("forbidden")},
                            {QStringLiteral("message"), QStringLiteral("not allowed")},
                        },
                        StatusCode::Forbidden
                    );
                }
                if (meMode == MeMode::Unauthorized) {
                    return QHttpServerResponse(
                        QJsonObject{
                            {
                                QStringLiteral("error"),
                                QStringLiteral("session_expired")
                            },
                            {QStringLiteral("message"), QStringLiteral("expired")},
                        },
                        StatusCode::Unauthorized
                    );
                }
                return QHttpServerResponse(QJsonObject{
                    {QStringLiteral("user"), userObject(false)},
                    {
                        QStringLiteral("expiresAt"),
                        QStringLiteral("2026-08-06T13:00:00Z")
                    },
                });
            }
        );
        server.route(
            QStringLiteral("/api/v1/auth/logout"),
            QHttpServerRequest::Method::Post,
            [this](const QHttpServerRequest& request) {
                lastAuthorization = request.value("Authorization");
                ++logoutCount;
                return QHttpServerResponse(QJsonObject{{
                    QStringLiteral("status"),
                    QStringLiteral("logged_out")
                }});
            }
        );

        if (!tcpServer.listen(QHostAddress::LocalHost, 0) ||
            !server.bind(&tcpServer)) {
            throw TestFailure("unable to start desktop auth test server");
        }
        port = tcpServer.serverPort();
    }

    QUrl baseUrl() const {
        return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(port));
    }

    static QJsonObject userObject(bool mustChangePassword) {
        return {
            {QStringLiteral("id"), 1},
            {QStringLiteral("username"), QStringLiteral("admin")},
            {QStringLiteral("displayName"), QStringLiteral("测试管理员")},
            {QStringLiteral("role"), QStringLiteral("admin")},
            {QStringLiteral("mustChangePassword"), mustChangePassword},
        };
    }
};

QString currentPageName(QStackedWidget* stack) {
    return stack->currentWidget() ? stack->currentWidget()->objectName() : QString();
}

void completeDesktopAuthenticationFlow() {
    DesktopAuthApi api;
    manage::desktop::MainWindow window(api.baseUrl());
    window.show();
    require(window.apiClient() != nullptr,
            "module integration must expose the shared ApiClient");

    auto* stack = requiredWidget<QStackedWidget>(window, "rootStack");
    auto* authTabs = requiredWidget<QTabWidget>(window, "authenticationTabs");
    auto* bootstrapName =
        requiredWidget<QLineEdit>(window, "bootstrapDisplayNameEdit");
    auto* bootstrapPassword =
        requiredWidget<QLineEdit>(window, "bootstrapPasswordEdit");
    auto* bootstrapConfirm =
        requiredWidget<QLineEdit>(window, "bootstrapConfirmEdit");
    auto* bootstrapButton =
        requiredWidget<QPushButton>(window, "bootstrapButton");

    require(currentPageName(stack) == QStringLiteral("authenticationPage"),
            "window must start on authentication page");
    authTabs->setCurrentIndex(1);
    bootstrapName->setText(QStringLiteral("测试管理员"));
    bootstrapPassword->setText(QStringLiteral("Temporary Password 1"));
    bootstrapConfirm->setText(QStringLiteral("Temporary Password 1"));
    bootstrapButton->click();
    require(!bootstrapButton->isEnabled(),
            "bootstrap controls must be disabled while request is running");

    waitUntil(
        [&]() {
            return currentPageName(stack) == QStringLiteral("passwordChangePage");
        },
        "bootstrap and automatic login must open password-change page"
    );
    require(
        api.lastBootstrapBody.value(QStringLiteral("displayName")).toString() ==
            QStringLiteral("测试管理员"),
        "bootstrap display name must reach ApiClient"
    );

    auto* currentPassword =
        requiredWidget<QLineEdit>(window, "currentPasswordEdit");
    auto* newPassword = requiredWidget<QLineEdit>(window, "newPasswordEdit");
    auto* confirmPassword =
        requiredWidget<QLineEdit>(window, "confirmPasswordEdit");
    auto* changeButton =
        requiredWidget<QPushButton>(window, "changePasswordButton");
    currentPassword->setText(QStringLiteral("Temporary Password 1"));
    newPassword->setText(QStringLiteral("Permanent Password 2"));
    confirmPassword->setText(QStringLiteral("Permanent Password 2"));
    changeButton->click();
    require(!changeButton->isEnabled(),
            "password-change controls must be disabled during request");

    waitUntil(
        [&]() { return currentPageName(stack) == QStringLiteral("mainPage"); },
        "password change must open main shell"
    );
    require(
        api.lastAuthorization == QByteArray("Bearer desktop-flow-token"),
        "password change must use ApiClient Bearer session"
    );
    require(
        api.lastChangePasswordBody.value(QStringLiteral("newPassword")).toString() ==
            QStringLiteral("Permanent Password 2"),
        "new password must be sent through ApiClient"
    );

    auto* userLabel = requiredWidget<QLabel>(window, "currentUserLabel");
    auto* roleLabel = requiredWidget<QLabel>(window, "currentRoleLabel");
    require(userLabel->text().contains(QStringLiteral("测试管理员")),
            "main shell must display current user");
    require(roleLabel->text().contains(QStringLiteral("管理员")),
            "main shell must display localized role");

    auto* moduleTabs = requiredWidget<QTabWidget>(window, "moduleTabs");
    const auto oldTabCount = moduleTabs->count();
    const auto moduleIndex = window.addModuleTab(
        QStringLiteral("测试模块"),
        new QLabel(QStringLiteral("模块内容"))
    );
    require(moduleIndex == oldTabCount, "module entry point must return new tab index");
    require(moduleTabs->count() == oldTabCount + 1,
            "module entry point must add a clean tab");

    auto* refreshButton =
        requiredWidget<QPushButton>(window, "refreshUserButton");
    auto* sessionMessage =
        requiredWidget<QLabel>(window, "sessionMessageLabel");
    api.meMode = DesktopAuthApi::MeMode::Forbidden;
    refreshButton->click();
    require(!refreshButton->isEnabled(),
            "identity controls must be disabled during refresh");
    waitUntil(
        [&]() { return sessionMessage->text().contains(QStringLiteral("没有权限")); },
        "403 must show a clear Chinese error"
    );
    require(currentPageName(stack) == QStringLiteral("mainPage"),
            "403 must retain the authenticated main shell");

    api.meMode = DesktopAuthApi::MeMode::Unauthorized;
    refreshButton->click();
    waitUntil(
        [&]() {
            return currentPageName(stack) == QStringLiteral("authenticationPage");
        },
        "401 must automatically return to login"
    );

    auto* loginUsername = requiredWidget<QLineEdit>(window, "loginUsernameEdit");
    auto* loginPassword = requiredWidget<QLineEdit>(window, "loginPasswordEdit");
    auto* loginButton = requiredWidget<QPushButton>(window, "loginButton");
    loginUsername->setText(QStringLiteral("admin"));
    loginPassword->setText(QStringLiteral("Permanent Password 2"));
    loginButton->click();
    require(!loginButton->isEnabled(),
            "login controls must be disabled while request is running");
    waitUntil(
        [&]() { return currentPageName(stack) == QStringLiteral("mainPage"); },
        "permanent password login must reopen main shell"
    );

    auto* logoutButton = requiredWidget<QPushButton>(window, "logoutButton");
    logoutButton->click();
    require(!logoutButton->isEnabled(),
            "logout controls must be disabled while request is running");
    waitUntil(
        [&]() {
            return currentPageName(stack) == QStringLiteral("authenticationPage");
        },
        "successful logout must return to login"
    );
    require(api.logoutCount == 1, "logout endpoint must be called exactly once");
    require(api.lastAuthorization == QByteArray("Bearer desktop-flow-token"),
            "logout must use the active ApiClient session");
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    try {
        completeDesktopAuthenticationFlow();
        std::cout << "[PASS] desktop bootstrap, login, password change, roles, 401/403, and logout\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] desktop authentication flow: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
