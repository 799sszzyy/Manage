#include "manage/server/api_server.h"

#include "manage/auth/auth_service.h"
#include "manage/auth/user_management.h"
#include "support/fake_user_repository.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {

class TestFailure final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void require(bool condition, const char* message) {
    if (!condition) throw TestFailure(message);
}

class FakeManagementRepository final : public manage::auth::UserManagementRepository {
public:
    FakeManagementRepository() {
        user.id = 2;
        user.username = QStringLiteral("quote.user");
        user.displayName = QStringLiteral("报价员甲");
        user.role = manage::auth::UserRole::Quoter;
        user.enabled = true;
        user.mustChangePassword = true;
        user.revision = 1;
    }

    manage::auth::UserManagementResult listUsers(
        const manage::auth::UserSearch& query
    ) override {
        lastSearch = query;
        manage::auth::UserManagementResult result;
        result.page.items.push_back(user);
        result.page.total = 1;
        result.page.page = query.page;
        result.page.pageSize = query.pageSize;
        return result;
    }
    manage::auth::UserManagementResult createUser(
        const manage::auth::CreateUserInput& input,
        const manage::auth::PasswordCredential& credential
    ) override {
        lastCreate = input;
        hashed = !credential.hash.isEmpty();
        auto result = success();
        result.user.username = input.username;
        return result;
    }
    manage::auth::UserManagementResult updateUser(
        const manage::auth::UpdateUserInput& input
    ) override {
        lastUpdate = input;
        return success();
    }
    manage::auth::UserManagementResult setUserEnabled(
        const manage::auth::SetUserEnabledInput& input
    ) override {
        lastEnabled = input;
        auto result = success();
        result.user.enabled = input.enabled;
        return result;
    }
    manage::auth::UserManagementResult resetUserPassword(
        const manage::auth::ResetUserPasswordInput& input,
        const manage::auth::PasswordCredential& credential
    ) override {
        lastReset = input;
        hashed = !credential.hash.isEmpty();
        return success();
    }
    manage::auth::UserManagementResult success() const {
        manage::auth::UserManagementResult result;
        result.user = user;
        return result;
    }
    manage::auth::ManagedUser user;
    manage::auth::UserSearch lastSearch;
    manage::auth::CreateUserInput lastCreate;
    manage::auth::UpdateUserInput lastUpdate;
    manage::auth::SetUserEnabledInput lastEnabled;
    manage::auth::ResetUserPasswordInput lastReset;
    bool hashed{};
};

struct Response final { int status{}; QJsonObject body; };

Response wait(QNetworkReply* reply) {
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&] { reply->abort(); loop.quit(); });
    timeout.start(5'000);
    if (!reply->isFinished()) loop.exec();
    Response response{
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),
        QJsonDocument::fromJson(reply->readAll()).object(),
    };
    reply->deleteLater();
    return response;
}

QNetworkReply* send(
    QNetworkAccessManager& network,
    quint16 port,
    const QString& token,
    const QByteArray& method,
    const QString& path,
    const QJsonObject& body = {}
) {
    QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(port).arg(path)));
    request.setRawHeader("Authorization", "Bearer " + token.toLatin1());
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (method == "GET") return network.get(request);
    return network.sendCustomRequest(request, method, QJsonDocument(body).toJson(QJsonDocument::Compact));
}

void adminRoutesAndRoleGate() {
    auto authRepository = std::make_shared<manage::tests::FakeUserRepository>();
    auto authService = std::make_shared<manage::auth::AuthService>(
        authRepository,
        manage::auth::PasswordHasher(manage::auth::PasswordHasher::kMinimumIterations)
    );
    require(authService->bootstrapAdministrator(QStringLiteral("Bootstrap Password 1")).succeeded(),
            "bootstrap admin");
    auto login = authService->login(QStringLiteral("admin"), QStringLiteral("Bootstrap Password 1"));
    require(login.succeeded(), "admin login");
    require(authService->changePassword(login.session.accessToken,
        QStringLiteral("Bootstrap Password 1"), QStringLiteral("Permanent Password 2")).succeeded(),
        "admin password changed");

    auto managementRepository = std::make_shared<FakeManagementRepository>();
    auto management = std::make_shared<manage::auth::UserManagementService>(
        managementRepository,
        manage::auth::PasswordHasher(manage::auth::PasswordHasher::kMinimumIterations)
    );
    manage::server::ApiServer server(authService, {}, nullptr, nullptr, management);
    const auto port = server.listen(QHostAddress::LocalHost, 0);
    require(port != 0, "server listens");
    QNetworkAccessManager network;

    auto listed = wait(send(network, port, login.session.accessToken, "GET",
                            QStringLiteral("/api/v1/users?search=quote&page=2&pageSize=10")));
    require(listed.status == 200, "admin lists users");
    require(managementRepository->lastSearch.search == QStringLiteral("quote") &&
            managementRepository->lastSearch.page == 2, "search and pagination forwarded");
    require(listed.body.value(QStringLiteral("items")).toArray().size() == 1,
            "list JSON returned");

    auto created = wait(send(network, port, login.session.accessToken, "POST",
        QStringLiteral("/api/v1/users"), QJsonObject{
            {QStringLiteral("username"), QStringLiteral("viewer.one")},
            {QStringLiteral("displayName"), QStringLiteral("查看员一")},
            {QStringLiteral("role"), QStringLiteral("viewer")},
            {QStringLiteral("temporaryPassword"), QStringLiteral("Temporary Password 3")},
        }));
    require(created.status == 201, "admin creates user");
    require(managementRepository->lastCreate.username == QStringLiteral("viewer.one") &&
            managementRepository->hashed, "create hashes and forwards input");

    auto updated = wait(send(network, port, login.session.accessToken, "PUT",
        QStringLiteral("/api/v1/users/2"), QJsonObject{
            {QStringLiteral("displayName"), QStringLiteral("新名称")},
            {QStringLiteral("role"), QStringLiteral("viewer")},
            {QStringLiteral("revision"), 1},
        }));
    require(updated.status == 200 && managementRepository->lastUpdate.actorUserId == 1,
            "update records authenticated administrator");

    authRepository->setRole(manage::auth::UserRole::Quoter);
    auto forbidden = wait(send(network, port, login.session.accessToken, "GET",
                               QStringLiteral("/api/v1/users")));
    require(forbidden.status == 403, "quoter cannot manage users");
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    try {
        adminRoutesAndRoleGate();
        std::cout << "1/1 user route tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
