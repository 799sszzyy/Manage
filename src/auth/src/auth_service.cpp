#include "manage/auth/auth_service.h"

#include <QCryptographicHash>
#include <QMutexLocker>
#include <QRandomGenerator>

#include <algorithm>
#include <utility>

namespace manage::auth {
namespace {

bool containsRole(
    std::initializer_list<UserRole> allowedRoles,
    UserRole actual
) {
    return std::find(allowedRoles.begin(), allowedRoles.end(), actual) !=
           allowedRoles.end();
}

bool isValidUsername(const QString& username) {
    if (username.isEmpty() || username.size() > 64) {
        return false;
    }
    for (const auto character : username) {
        const auto value = character.unicode();
        const auto allowed = (value >= 'a' && value <= 'z') ||
                             (value >= 'A' && value <= 'Z') ||
                             (value >= '0' && value <= '9') ||
                             value == '.' || value == '_' || value == '-';
        if (!allowed) {
            return false;
        }
    }
    return true;
}

} // namespace

QString roleCode(UserRole role) {
    switch (role) {
    case UserRole::Admin:
        return QStringLiteral("admin");
    case UserRole::Quoter:
        return QStringLiteral("quoter");
    case UserRole::Viewer:
        return QStringLiteral("viewer");
    }
    return QStringLiteral("viewer");
}

QString authErrorCode(AuthErrorCode code) {
    switch (code) {
    case AuthErrorCode::None:
        return {};
    case AuthErrorCode::InvalidRequest:
        return QStringLiteral("invalid_request");
    case AuthErrorCode::BootstrapUnavailable:
        return QStringLiteral("bootstrap_unavailable");
    case AuthErrorCode::InvalidCredentials:
        return QStringLiteral("invalid_credentials");
    case AuthErrorCode::AccountDisabled:
        return QStringLiteral("account_disabled");
    case AuthErrorCode::Unauthorized:
        return QStringLiteral("unauthorized");
    case AuthErrorCode::SessionExpired:
        return QStringLiteral("session_expired");
    case AuthErrorCode::PasswordChangeRequired:
        return QStringLiteral("password_change_required");
    case AuthErrorCode::Forbidden:
        return QStringLiteral("forbidden");
    case AuthErrorCode::RepositoryFailure:
        return QStringLiteral("repository_failure");
    }
    return QStringLiteral("authentication_error");
}

AuthService::AuthService(
    std::shared_ptr<UserRepository> repository,
    PasswordHasher passwordHasher,
    std::chrono::seconds sessionLifetime
) : repository_(std::move(repository)),
    passwordHasher_(std::move(passwordHasher)),
    sessionLifetime_(sessionLifetime) {}

AuthResult AuthService::bootstrapAdministrator(
    const QString& password,
    const QString& displayName
) {
    if (!repository_) {
        return failure(
            AuthErrorCode::RepositoryFailure,
            QStringLiteral("authentication storage is unavailable")
        );
    }
    if (displayName.trimmed().isEmpty() || displayName.size() > 100) {
        return failure(
            AuthErrorCode::InvalidRequest,
            QStringLiteral("displayName must contain between 1 and 100 characters")
        );
    }
    const auto policyError = PasswordHasher::passwordPolicyError(password);
    if (!policyError.isEmpty()) {
        return failure(AuthErrorCode::InvalidRequest, policyError);
    }

    const auto credential = passwordHasher_.create(password);
    UserAccount account;
    QString repositoryError;
    const auto result = repository_->bootstrapAdministrator(
        displayName.trimmed(),
        credential,
        &account,
        &repositoryError
    );
    if (result == RepositoryResult::Conflict) {
        return failure(
            AuthErrorCode::BootstrapUnavailable,
            QStringLiteral("the initial administrator is already configured")
        );
    }
    if (result != RepositoryResult::Success) {
        Q_UNUSED(repositoryError);
        return failure(
            AuthErrorCode::RepositoryFailure,
            QStringLiteral("unable to configure the initial administrator")
        );
    }

    AuthResult response;
    response.session.user = publicUser(account);
    return response;
}

AuthResult AuthService::login(
    const QString& username,
    const QString& password
) {
    if (!repository_) {
        return failure(
            AuthErrorCode::RepositoryFailure,
            QStringLiteral("authentication storage is unavailable")
        );
    }
    const auto normalizedUsername = username.trimmed();
    if (!isValidUsername(normalizedUsername) || password.isEmpty() ||
        password.toUtf8().size() > 1024) {
        return failure(
            AuthErrorCode::InvalidRequest,
            QStringLiteral("username or password has an invalid format")
        );
    }

    UserAccount account;
    QString repositoryError;
    const auto result = repository_->findByUsername(
        normalizedUsername,
        &account,
        &repositoryError
    );
    if (result == RepositoryResult::NotFound) {
        return failure(
            AuthErrorCode::InvalidCredentials,
            QStringLiteral("username or password is incorrect")
        );
    }
    if (result != RepositoryResult::Success) {
        Q_UNUSED(repositoryError);
        return failure(
            AuthErrorCode::RepositoryFailure,
            QStringLiteral("unable to read the account")
        );
    }
    if (!account.enabled) {
        return failure(
            AuthErrorCode::AccountDisabled,
            QStringLiteral("this account is disabled")
        );
    }
    if (!passwordHasher_.verify(password, account.credential)) {
        return failure(
            AuthErrorCode::InvalidCredentials,
            QStringLiteral("username or password is incorrect")
        );
    }

    // Only the random token is returned to the client. The server stores a
    // digest as the session key, so an in-memory dump does not reveal a token
    // that can be replayed directly.
    SessionInfo session;
    session.accessToken = generateAccessToken();
    session.expiresAtUtc = QDateTime::currentDateTimeUtc().addSecs(
        sessionLifetime_.count()
    );
    session.user = publicUser(account);
    {
        QMutexLocker locker(&sessionsMutex_);
        sessions_.insert(
            tokenKey(session.accessToken),
            StoredSession{account.id, session.expiresAtUtc}
        );
    }

    AuthResult response;
    response.session = std::move(session);
    return response;
}

AuthResult AuthService::logout(const QString& accessToken) {
    if (accessToken.isEmpty()) {
        return failure(
            AuthErrorCode::Unauthorized,
            QStringLiteral("a bearer token is required")
        );
    }
    QMutexLocker locker(&sessionsMutex_);
    if (sessions_.remove(tokenKey(accessToken)) != 1) {
        return failure(
            AuthErrorCode::Unauthorized,
            QStringLiteral("the session token is invalid")
        );
    }
    return {};
}

AuthResult AuthService::currentUser(const QString& accessToken) {
    return resolveSession(accessToken);
}

AuthResult AuthService::changePassword(
    const QString& accessToken,
    const QString& currentPassword,
    const QString& newPassword
) {
    const auto sessionResult = resolveSession(accessToken);
    if (!sessionResult.succeeded()) {
        return sessionResult;
    }
    if (currentPassword.isEmpty() || currentPassword.toUtf8().size() > 1024) {
        return failure(
            AuthErrorCode::InvalidRequest,
            QStringLiteral("currentPassword has an invalid format")
        );
    }
    const auto policyError = PasswordHasher::passwordPolicyError(newPassword);
    if (!policyError.isEmpty()) {
        return failure(AuthErrorCode::InvalidRequest, policyError);
    }
    if (currentPassword == newPassword) {
        return failure(
            AuthErrorCode::InvalidRequest,
            QStringLiteral("newPassword must be different from currentPassword")
        );
    }

    UserAccount account;
    QString repositoryError;
    const auto findResult = repository_->findById(
        sessionResult.session.user.id,
        &account,
        &repositoryError
    );
    if (findResult == RepositoryResult::NotFound || !account.enabled) {
        return failure(
            AuthErrorCode::Unauthorized,
            QStringLiteral("the account for this session is unavailable")
        );
    }
    if (findResult != RepositoryResult::Success) {
        Q_UNUSED(repositoryError);
        return failure(
            AuthErrorCode::RepositoryFailure,
            QStringLiteral("unable to read the account")
        );
    }
    if (!passwordHasher_.verify(currentPassword, account.credential)) {
        return failure(
            AuthErrorCode::InvalidCredentials,
            QStringLiteral("current password is incorrect")
        );
    }

    const auto credential = passwordHasher_.create(newPassword);
    UserAccount updated;
    const auto updateResult = repository_->changePassword(
        account.id,
        account.credential.hash,
        credential,
        &updated,
        &repositoryError
    );
    if (updateResult == RepositoryResult::Conflict) {
        return failure(
            AuthErrorCode::InvalidCredentials,
            QStringLiteral("the password changed concurrently; please log in again")
        );
    }
    if (updateResult != RepositoryResult::Success) {
        Q_UNUSED(repositoryError);
        return failure(
            AuthErrorCode::RepositoryFailure,
            QStringLiteral("unable to change the password")
        );
    }

    AuthResult result;
    result.session.expiresAtUtc = sessionResult.session.expiresAtUtc;
    result.session.user = publicUser(updated);
    return result;
}

AuthResult AuthService::authorize(
    const QString& accessToken,
    std::initializer_list<UserRole> allowedRoles
) {
    auto result = resolveSession(accessToken);
    if (!result.succeeded()) {
        return result;
    }
    if (result.session.user.mustChangePassword) {
        return failure(
            AuthErrorCode::PasswordChangeRequired,
            QStringLiteral("the password must be changed before this operation")
        );
    }
    if (!containsRole(allowedRoles, result.session.user.role)) {
        return failure(
            AuthErrorCode::Forbidden,
            QStringLiteral("this account does not have permission")
        );
    }
    return result;
}

AuthResult AuthService::failure(AuthErrorCode code, const QString& message) {
    AuthResult result;
    result.error = code;
    result.message = message;
    return result;
}

AuthenticatedUser AuthService::publicUser(const UserAccount& account) {
    return {
        account.id,
        account.username,
        account.displayName,
        account.role,
        account.mustChangePassword,
    };
}

QByteArray AuthService::tokenKey(const QString& accessToken) {
    return QCryptographicHash::hash(
        accessToken.toUtf8(),
        QCryptographicHash::Sha256
    );
}

QString AuthService::generateAccessToken() {
    QByteArray bytes(32, Qt::Uninitialized);
    auto* generator = QRandomGenerator::system();
    for (qsizetype index = 0; index < bytes.size(); ++index) {
        bytes[index] = static_cast<char>(generator->generate() & 0xffU);
    }
    return QString::fromLatin1(
        bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)
    );
}

AuthResult AuthService::resolveSession(const QString& accessToken) {
    if (!repository_) {
        return failure(
            AuthErrorCode::RepositoryFailure,
            QStringLiteral("authentication storage is unavailable")
        );
    }
    if (accessToken.isEmpty()) {
        return failure(
            AuthErrorCode::Unauthorized,
            QStringLiteral("a bearer token is required")
        );
    }

    StoredSession stored;
    const auto key = tokenKey(accessToken);
    {
        QMutexLocker locker(&sessionsMutex_);
        const auto iterator = sessions_.find(key);
        if (iterator == sessions_.end()) {
            return failure(
                AuthErrorCode::Unauthorized,
                QStringLiteral("the session token is invalid")
            );
        }
        if (iterator->expiresAtUtc <= QDateTime::currentDateTimeUtc()) {
            sessions_.erase(iterator);
            return failure(
                AuthErrorCode::SessionExpired,
                QStringLiteral("the session has expired")
            );
        }
        stored = *iterator;
    }

    UserAccount account;
    QString repositoryError;
    const auto repositoryResult = repository_->findById(
        stored.userId,
        &account,
        &repositoryError
    );
    if (repositoryResult == RepositoryResult::NotFound || !account.enabled) {
        QMutexLocker locker(&sessionsMutex_);
        sessions_.remove(key);
        return failure(
            AuthErrorCode::Unauthorized,
            QStringLiteral("the account for this session is unavailable")
        );
    }
    if (repositoryResult != RepositoryResult::Success) {
        Q_UNUSED(repositoryError);
        return failure(
            AuthErrorCode::RepositoryFailure,
            QStringLiteral("unable to read the account")
        );
    }

    AuthResult result;
    result.session.expiresAtUtc = stored.expiresAtUtc;
    result.session.user = publicUser(account);
    return result;
}

} // namespace manage::auth
