#pragma once

#include "manage/auth/user_repository.h"

#include <QString>

namespace manage::tests {

class FakeUserRepository final : public manage::auth::UserRepository {
public:
    FakeUserRepository() {
        account_.id = 1;
        account_.username = QStringLiteral("admin");
        account_.displayName = QStringLiteral("初始管理员");
        account_.role = manage::auth::UserRole::Admin;
        account_.mustChangePassword = true;
        account_.enabled = false;
    }

    manage::auth::RepositoryResult findByUsername(
        const QString& username,
        manage::auth::UserAccount* account,
        QString* errorMessage
    ) override {
        clearError(errorMessage);
        if (username != account_.username) {
            return manage::auth::RepositoryResult::NotFound;
        }
        *account = account_;
        return manage::auth::RepositoryResult::Success;
    }

    manage::auth::RepositoryResult findById(
        quint64 id,
        manage::auth::UserAccount* account,
        QString* errorMessage
    ) override {
        clearError(errorMessage);
        if (id != account_.id) {
            return manage::auth::RepositoryResult::NotFound;
        }
        *account = account_;
        return manage::auth::RepositoryResult::Success;
    }

    manage::auth::RepositoryResult bootstrapAdministrator(
        const QString& displayName,
        const manage::auth::PasswordCredential& credential,
        manage::auth::UserAccount* account,
        QString* errorMessage
    ) override {
        clearError(errorMessage);
        if (account_.enabled || !account_.credential.hash.isEmpty()) {
            return manage::auth::RepositoryResult::Conflict;
        }
        account_.displayName = displayName;
        account_.credential = credential;
        account_.mustChangePassword = true;
        account_.enabled = true;
        *account = account_;
        return manage::auth::RepositoryResult::Success;
    }

    manage::auth::RepositoryResult changePassword(
        quint64 userId,
        const QByteArray& expectedPasswordHash,
        const manage::auth::PasswordCredential& credential,
        manage::auth::UserAccount* account,
        QString* errorMessage
    ) override {
        clearError(errorMessage);
        if (userId != account_.id || !account_.enabled) {
            return manage::auth::RepositoryResult::NotFound;
        }
        if (expectedPasswordHash != account_.credential.hash) {
            return manage::auth::RepositoryResult::Conflict;
        }
        account_.credential = credential;
        account_.mustChangePassword = false;
        *account = account_;
        return manage::auth::RepositoryResult::Success;
    }

    void disableAccount() { account_.enabled = false; }
    void setRole(manage::auth::UserRole role) { account_.role = role; }

private:
    static void clearError(QString* errorMessage) {
        if (errorMessage != nullptr) {
            errorMessage->clear();
        }
    }

    manage::auth::UserAccount account_;
};

} // namespace manage::tests
