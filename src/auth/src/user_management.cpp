#include "manage/auth/user_management.h"

#include <utility>

namespace manage::auth {

UserManagementService::UserManagementService(
    std::shared_ptr<UserManagementRepository> repository,
    PasswordHasher passwordHasher
) : repository_(std::move(repository)),
    passwordHasher_(std::move(passwordHasher)) {}

bool UserManagementService::validUsername(const QString& username) {
    if (username.isEmpty() || username.size() > 64) {
        return false;
    }
    for (const auto character : username) {
        const auto value = character.unicode();
        if (!((value >= 'a' && value <= 'z') ||
              (value >= 'A' && value <= 'Z') ||
              (value >= '0' && value <= '9') || value == '.' || value == '_' ||
              value == '-')) {
            return false;
        }
    }
    return true;
}

UserManagementResult UserManagementService::listUsers(UserSearch query) const {
    if (!repository_) {
        return failure(UserManagementError::RepositoryFailure,
                       QStringLiteral("user storage is unavailable"));
    }
    query.search = query.search.trimmed();
    if (query.search.size() > 100 || query.page < 1 || query.pageSize < 1 ||
        query.pageSize > 100) {
        return failure(UserManagementError::Validation,
                       QStringLiteral("invalid search or pagination"));
    }
    return repository_->listUsers(query);
}

UserManagementResult UserManagementService::createUser(CreateUserInput input) const {
    if (!repository_) {
        return failure(UserManagementError::RepositoryFailure,
                       QStringLiteral("user storage is unavailable"));
    }
    input.username = input.username.trimmed();
    input.displayName = input.displayName.trimmed();
    if (!validUsername(input.username)) {
        return failure(UserManagementError::Validation,
                       QStringLiteral("username has an invalid format"));
    }
    if (input.displayName.isEmpty() || input.displayName.size() > 100) {
        return failure(UserManagementError::Validation,
                       QStringLiteral("displayName must contain 1 to 100 characters"));
    }
    const auto passwordError = PasswordHasher::passwordPolicyError(
        input.temporaryPassword
    );
    if (!passwordError.isEmpty()) {
        return failure(UserManagementError::Validation, passwordError);
    }
    return repository_->createUser(
        input, passwordHasher_.create(input.temporaryPassword)
    );
}

UserManagementResult UserManagementService::updateUser(UpdateUserInput input) const {
    if (!repository_) {
        return failure(UserManagementError::RepositoryFailure,
                       QStringLiteral("user storage is unavailable"));
    }
    input.displayName = input.displayName.trimmed();
    if (input.id == 0 || input.actorUserId == 0 || input.revision < 1 ||
        input.displayName.isEmpty() || input.displayName.size() > 100) {
        return failure(UserManagementError::Validation,
                       QStringLiteral("invalid user update"));
    }
    return repository_->updateUser(input);
}

UserManagementResult UserManagementService::setUserEnabled(
    SetUserEnabledInput input
) const {
    if (!repository_) {
        return failure(UserManagementError::RepositoryFailure,
                       QStringLiteral("user storage is unavailable"));
    }
    if (input.id == 0 || input.actorUserId == 0 || input.revision < 1) {
        return failure(UserManagementError::Validation,
                       QStringLiteral("invalid enabled-state update"));
    }
    if (!input.enabled && input.id == input.actorUserId) {
        return failure(UserManagementError::ProtectedAccount,
                       QStringLiteral("the current administrator cannot be disabled"));
    }
    return repository_->setUserEnabled(input);
}

UserManagementResult UserManagementService::resetUserPassword(
    ResetUserPasswordInput input
) const {
    if (!repository_) {
        return failure(UserManagementError::RepositoryFailure,
                       QStringLiteral("user storage is unavailable"));
    }
    if (input.id == 0 || input.revision < 1) {
        return failure(UserManagementError::Validation,
                       QStringLiteral("invalid password reset"));
    }
    const auto passwordError = PasswordHasher::passwordPolicyError(
        input.temporaryPassword
    );
    if (!passwordError.isEmpty()) {
        return failure(UserManagementError::Validation, passwordError);
    }
    return repository_->resetUserPassword(
        input, passwordHasher_.create(input.temporaryPassword)
    );
}

UserManagementResult UserManagementService::failure(
    UserManagementError error,
    const QString& message
) {
    UserManagementResult result;
    result.error = error;
    result.message = message;
    return result;
}

} // namespace manage::auth
