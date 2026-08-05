#pragma once

#include "manage/auth/user_repository.h"
#include "manage/auth/user_management.h"

#include <QSqlDatabase>

namespace manage::data {

class MySqlUserRepository final : public manage::auth::UserRepository,
                                  public manage::auth::UserManagementRepository {
public:
    explicit MySqlUserRepository(QSqlDatabase database);

    manage::auth::RepositoryResult findByUsername(
        const QString& username,
        manage::auth::UserAccount* account,
        QString* errorMessage
    ) override;

    manage::auth::RepositoryResult findById(
        quint64 id,
        manage::auth::UserAccount* account,
        QString* errorMessage
    ) override;

    manage::auth::RepositoryResult bootstrapAdministrator(
        const QString& displayName,
        const manage::auth::PasswordCredential& credential,
        manage::auth::UserAccount* account,
        QString* errorMessage
    ) override;

    manage::auth::RepositoryResult changePassword(
        quint64 userId,
        const QByteArray& expectedPasswordHash,
        const manage::auth::PasswordCredential& credential,
        manage::auth::UserAccount* account,
        QString* errorMessage
    ) override;

    manage::auth::UserManagementResult listUsers(
        const manage::auth::UserSearch& query
    ) override;
    manage::auth::UserManagementResult createUser(
        const manage::auth::CreateUserInput& input,
        const manage::auth::PasswordCredential& credential
    ) override;
    manage::auth::UserManagementResult updateUser(
        const manage::auth::UpdateUserInput& input
    ) override;
    manage::auth::UserManagementResult setUserEnabled(
        const manage::auth::SetUserEnabledInput& input
    ) override;
    manage::auth::UserManagementResult resetUserPassword(
        const manage::auth::ResetUserPasswordInput& input,
        const manage::auth::PasswordCredential& credential
    ) override;

private:
    QSqlDatabase database_;
};

} // namespace manage::data
