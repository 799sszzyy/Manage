#pragma once

#include "manage/auth/auth_types.h"
#include "manage/auth/password_hasher.h"
#include "manage/auth/user_repository.h"

#include <QDateTime>
#include <QHash>
#include <QMutex>
#include <QString>

#include <chrono>
#include <initializer_list>
#include <memory>

namespace manage::auth {

class AuthService final {
public:
    explicit AuthService(
        std::shared_ptr<UserRepository> repository,
        PasswordHasher passwordHasher = PasswordHasher{},
        std::chrono::seconds sessionLifetime = std::chrono::hours(8)
    );

    AuthResult bootstrapAdministrator(
        const QString& password,
        const QString& displayName = QStringLiteral("初始管理员")
    );
    AuthResult login(const QString& username, const QString& password);
    AuthResult logout(const QString& accessToken);
    AuthResult currentUser(const QString& accessToken);
    AuthResult changePassword(
        const QString& accessToken,
        const QString& currentPassword,
        const QString& newPassword
    );
    AuthResult authorize(
        const QString& accessToken,
        std::initializer_list<UserRole> allowedRoles
    );

private:
    struct StoredSession final {
        quint64 userId{};
        QDateTime expiresAtUtc;
    };

    static AuthResult failure(AuthErrorCode code, const QString& message);
    static AuthenticatedUser publicUser(const UserAccount& account);
    static QByteArray tokenKey(const QString& accessToken);
    static QString generateAccessToken();
    AuthResult resolveSession(const QString& accessToken);

    std::shared_ptr<UserRepository> repository_;
    PasswordHasher passwordHasher_;
    std::chrono::seconds sessionLifetime_;
    QMutex sessionsMutex_;
    QHash<QByteArray, StoredSession> sessions_;
};

} // namespace manage::auth
