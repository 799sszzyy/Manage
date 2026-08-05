#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QtGlobal>

namespace manage::auth {

enum class UserRole {
    Admin,
    Quoter,
    Viewer,
};

QString roleCode(UserRole role);

struct PasswordCredential final {
    QString algorithm;
    QByteArray hash;
    QByteArray salt;
    int iterations{};
};

struct UserAccount final {
    quint64 id{};
    QString username;
    QString displayName;
    UserRole role{UserRole::Viewer};
    PasswordCredential credential;
    bool mustChangePassword{true};
    bool enabled{false};
};

struct AuthenticatedUser final {
    quint64 id{};
    QString username;
    QString displayName;
    UserRole role{UserRole::Viewer};
    bool mustChangePassword{true};
};

struct SessionInfo final {
    QString accessToken;
    QDateTime expiresAtUtc;
    AuthenticatedUser user;
};

enum class AuthErrorCode {
    None,
    InvalidRequest,
    BootstrapUnavailable,
    InvalidCredentials,
    AccountDisabled,
    Unauthorized,
    SessionExpired,
    PasswordChangeRequired,
    Forbidden,
    RepositoryFailure,
};

QString authErrorCode(AuthErrorCode code);

struct AuthResult final {
    AuthErrorCode error{AuthErrorCode::None};
    QString message;
    SessionInfo session;

    bool succeeded() const { return error == AuthErrorCode::None; }
};

} // namespace manage::auth
