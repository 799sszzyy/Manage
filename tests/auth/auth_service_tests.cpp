#include "manage/auth/auth_service.h"
#include "manage/auth/password_hasher.h"
#include "support/fake_user_repository.h"

#include <QCoreApplication>

#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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

void pbkdf2MatchesKnownSha256Vector() {
    const auto derived = manage::auth::PasswordHasher::derivePbkdf2Sha256(
        QByteArrayLiteral("password"),
        QByteArrayLiteral("salt"),
        1
    );
    require(
        derived.toHex() == QByteArrayLiteral(
            "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b"
        ),
        "PBKDF2-HMAC-SHA256 known vector"
    );
    require(
        manage::auth::PasswordHasher::constantTimeEquals(derived, derived),
        "constant-time comparison accepts equal values"
    );
    require(
        !manage::auth::PasswordHasher::constantTimeEquals(
            derived,
            derived + QByteArrayLiteral("x")
        ),
        "constant-time comparison rejects different lengths"
    );
}

void bootstrapLoginAuthorizationAndLogout() {
    auto repository = std::make_shared<manage::tests::FakeUserRepository>();
    manage::auth::AuthService service(
        repository,
        manage::auth::PasswordHasher(
            manage::auth::PasswordHasher::kMinimumIterations
        )
    );

    const auto weak = service.bootstrapAdministrator(QStringLiteral("short"));
    require(
        weak.error == manage::auth::AuthErrorCode::InvalidRequest,
        "weak bootstrap password is rejected"
    );

    const auto bootstrap = service.bootstrapAdministrator(
        QStringLiteral("Correct Horse Battery 1"),
        QStringLiteral("系统管理员")
    );
    require(bootstrap.succeeded(), bootstrap.message.toStdString());
    require(bootstrap.session.user.username == QStringLiteral("admin"), "admin user");
    require(
        bootstrap.session.user.mustChangePassword,
        "bootstrap password remains temporary"
    );

    const auto repeated = service.bootstrapAdministrator(
        QStringLiteral("Another Secure Password 2")
    );
    require(
        repeated.error == manage::auth::AuthErrorCode::BootstrapUnavailable,
        "bootstrap is one-time only"
    );

    const auto wrong = service.login(
        QStringLiteral("admin"),
        QStringLiteral("Wrong Password Value")
    );
    require(
        wrong.error == manage::auth::AuthErrorCode::InvalidCredentials,
        "wrong password is rejected"
    );

    const auto login = service.login(
        QStringLiteral("admin"),
        QStringLiteral("Correct Horse Battery 1")
    );
    require(login.succeeded(), login.message.toStdString());
    require(login.session.accessToken.size() >= 40, "random bearer token returned");

    const auto me = service.currentUser(login.session.accessToken);
    require(me.succeeded(), me.message.toStdString());
    require(me.session.user.role == manage::auth::UserRole::Admin, "admin role retained");
    require(me.session.user.mustChangePassword, "first login requires a password change");

    const auto blocked = service.authorize(
        login.session.accessToken,
        {manage::auth::UserRole::Admin}
    );
    require(
        blocked.error == manage::auth::AuthErrorCode::PasswordChangeRequired,
        "business authorization is blocked before password change"
    );
    require(
        manage::auth::authErrorCode(blocked.error) ==
            QStringLiteral("password_change_required"),
        "forced password change has a stable error code"
    );

    const auto wrongCurrent = service.changePassword(
        login.session.accessToken,
        QStringLiteral("Incorrect Current Password"),
        QStringLiteral("Replaced Horse Battery 2")
    );
    require(
        wrongCurrent.error == manage::auth::AuthErrorCode::InvalidCredentials,
        "password change verifies the current password"
    );
    const auto changed = service.changePassword(
        login.session.accessToken,
        QStringLiteral("Correct Horse Battery 1"),
        QStringLiteral("Replaced Horse Battery 2")
    );
    require(changed.succeeded(), changed.message.toStdString());
    require(
        !changed.session.user.mustChangePassword,
        "successful change clears the forced-change flag"
    );

    const auto allowed = service.authorize(
        login.session.accessToken,
        {manage::auth::UserRole::Admin}
    );
    require(allowed.succeeded(), "admin permission accepted");
    const auto forbidden = service.authorize(
        login.session.accessToken,
        {manage::auth::UserRole::Quoter, manage::auth::UserRole::Viewer}
    );
    require(
        forbidden.error == manage::auth::AuthErrorCode::Forbidden,
        "role authorization rejects an unlisted role"
    );

    require(service.logout(login.session.accessToken).succeeded(), "logout succeeds");
    require(
        service.currentUser(login.session.accessToken).error ==
            manage::auth::AuthErrorCode::Unauthorized,
        "logged-out token cannot be reused"
    );

    require(
        service.login(
            QStringLiteral("admin"),
            QStringLiteral("Correct Horse Battery 1")
        ).error == manage::auth::AuthErrorCode::InvalidCredentials,
        "old password stops working"
    );
    require(
        service.login(
            QStringLiteral("admin"),
            QStringLiteral("Replaced Horse Battery 2")
        ).succeeded(),
        "new password can log in"
    );
}

void disabledAccountInvalidatesExistingSession() {
    auto repository = std::make_shared<manage::tests::FakeUserRepository>();
    manage::auth::AuthService service(
        repository,
        manage::auth::PasswordHasher(
            manage::auth::PasswordHasher::kMinimumIterations
        )
    );
    require(
        service.bootstrapAdministrator(
            QStringLiteral("Correct Horse Battery 1")
        ).succeeded(),
        "bootstrap before disable test"
    );
    const auto login = service.login(
        QStringLiteral("admin"),
        QStringLiteral("Correct Horse Battery 1")
    );
    require(login.succeeded(), "login before disable test");

    repository->disableAccount();
    require(
        service.currentUser(login.session.accessToken).error ==
            manage::auth::AuthErrorCode::Unauthorized,
        "disabling account invalidates its session"
    );
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);

    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"PBKDF2 vector", pbkdf2MatchesKnownSha256Vector},
        {"auth lifecycle", bootstrapLoginAuthorizationAndLogout},
        {"disabled account", disabledAccountInvalidatesExistingSession},
    };

    std::size_t passed = 0;
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
}
