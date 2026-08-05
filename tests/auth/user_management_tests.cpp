#include "manage/auth/user_management.h"

#include <QCoreApplication>

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

class FakeRepository final : public manage::auth::UserManagementRepository {
public:
    manage::auth::UserManagementResult listUsers(
        const manage::auth::UserSearch& query
    ) override {
        lastSearch = query;
        manage::auth::UserManagementResult result;
        result.page.page = query.page;
        result.page.pageSize = query.pageSize;
        result.page.total = 1;
        result.page.items.push_back(user);
        return result;
    }

    manage::auth::UserManagementResult createUser(
        const manage::auth::CreateUserInput& input,
        const manage::auth::PasswordCredential& credential
    ) override {
        ++createCalls;
        lastCreate = input;
        lastCredential = credential;
        auto result = success();
        result.user.username = input.username;
        result.user.displayName = input.displayName;
        result.user.role = input.role;
        result.user.mustChangePassword = true;
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
        ++enabledCalls;
        lastEnabled = input;
        return success();
    }

    manage::auth::UserManagementResult resetUserPassword(
        const manage::auth::ResetUserPasswordInput& input,
        const manage::auth::PasswordCredential& credential
    ) override {
        ++resetCalls;
        lastReset = input;
        lastCredential = credential;
        auto result = success();
        result.user.mustChangePassword = true;
        return result;
    }

    manage::auth::UserManagementResult success() const {
        manage::auth::UserManagementResult result;
        result.user = user;
        return result;
    }

    manage::auth::ManagedUser user{2, QStringLiteral("quote.user"),
        QStringLiteral("报价员"), manage::auth::UserRole::Quoter, true, true, 3};
    manage::auth::UserSearch lastSearch;
    manage::auth::CreateUserInput lastCreate;
    manage::auth::UpdateUserInput lastUpdate;
    manage::auth::SetUserEnabledInput lastEnabled;
    manage::auth::ResetUserPasswordInput lastReset;
    manage::auth::PasswordCredential lastCredential;
    int createCalls{};
    int enabledCalls{};
    int resetCalls{};
};

void validationHashingAndForwarding() {
    auto repository = std::make_shared<FakeRepository>();
    manage::auth::UserManagementService service(
        repository,
        manage::auth::PasswordHasher(manage::auth::PasswordHasher::kMinimumIterations)
    );

    auto invalid = service.createUser({QStringLiteral("bad user"),
        QStringLiteral("Bad"), manage::auth::UserRole::Viewer,
        QStringLiteral("Strong Temporary 1")});
    require(invalid.error == manage::auth::UserManagementError::Validation,
            "invalid username rejected");
    require(repository->createCalls == 0, "invalid create not persisted");

    auto created = service.createUser({QStringLiteral("  quote.user  "),
        QStringLiteral("  报价员甲  "), manage::auth::UserRole::Quoter,
        QStringLiteral("Strong Temporary 1")});
    require(created.ok(), "valid user created");
    require(repository->lastCreate.username == QStringLiteral("quote.user"),
            "username normalized");
    require(repository->lastCreate.displayName == QStringLiteral("报价员甲"),
            "display name normalized");
    require(!repository->lastCredential.hash.isEmpty(), "PBKDF2 hash generated");
    require(repository->lastCredential.algorithm == QStringLiteral("pbkdf2-sha256"),
            "existing PBKDF2 algorithm reused");
    require(created.user.mustChangePassword, "new password is temporary");

    auto listed = service.listUsers({QStringLiteral(" quote "), 2, 10});
    require(listed.ok() && repository->lastSearch.search == QStringLiteral("quote"),
            "search normalized and forwarded");

    auto updated = service.updateUser({2, QStringLiteral("  新名称  "),
        manage::auth::UserRole::Viewer, 3, 1});
    require(updated.ok() && repository->lastUpdate.displayName == QStringLiteral("新名称"),
            "editable fields forwarded without username");

    auto reset = service.resetUserPassword({2, 3, QStringLiteral("Reset Temporary 2")});
    require(reset.ok() && repository->resetCalls == 1,
            "temporary password reset persisted");
    require(reset.user.mustChangePassword, "reset requires next-login password change");
}

void protectsCurrentAdministratorBeforeRepositoryWrite() {
    auto repository = std::make_shared<FakeRepository>();
    manage::auth::UserManagementService service(repository);
    const auto result = service.setUserEnabled({1, false, 2, 1});
    require(result.error == manage::auth::UserManagementError::ProtectedAccount,
            "current administrator cannot disable itself");
    require(repository->enabledCalls == 0, "protected change not written");
    require(service.setUserEnabled({2, false, 3, 1}).ok(),
            "another account can be disabled");
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    try {
        validationHashingAndForwarding();
        protectsCurrentAdministratorBeforeRepositoryWrite();
        std::cout << "2/2 user management tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
