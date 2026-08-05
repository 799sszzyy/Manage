#pragma once

#include "manage/auth/user_repository.h"

#include <QSqlDatabase>

namespace manage::data {

class MySqlUserRepository final : public manage::auth::UserRepository {
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

private:
    QSqlDatabase database_;
};

} // namespace manage::data
