#pragma once

#include "manage/auth/auth_types.h"

#include <QString>

namespace manage::auth {

enum class RepositoryResult {
    Success,
    NotFound,
    Conflict,
    Failure,
};

class UserRepository {
public:
    virtual ~UserRepository() = default;

    virtual RepositoryResult findByUsername(
        const QString& username,
        UserAccount* account,
        QString* errorMessage
    ) = 0;

    virtual RepositoryResult findById(
        quint64 id,
        UserAccount* account,
        QString* errorMessage
    ) = 0;

    virtual RepositoryResult bootstrapAdministrator(
        const QString& displayName,
        const PasswordCredential& credential,
        UserAccount* account,
        QString* errorMessage
    ) = 0;

    virtual RepositoryResult changePassword(
        quint64 userId,
        const QByteArray& expectedPasswordHash,
        const PasswordCredential& credential,
        UserAccount* account,
        QString* errorMessage
    ) = 0;
};

} // namespace manage::auth
