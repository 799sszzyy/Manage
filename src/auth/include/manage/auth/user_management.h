#pragma once

#include "manage/auth/auth_types.h"
#include "manage/auth/password_hasher.h"

#include <QDateTime>
#include <QString>
#include <QtGlobal>

#include <memory>
#include <vector>

namespace manage::auth {

enum class UserManagementError {
    None,
    Validation,
    NotFound,
    Conflict,
    ProtectedAccount,
    RepositoryFailure,
};

struct ManagedUser final {
    quint64 id{};
    QString username;
    QString displayName;
    UserRole role{UserRole::Viewer};
    bool enabled{};
    bool mustChangePassword{};
    int revision{};
    QDateTime createdAt;
    QDateTime updatedAt;
};

struct UserSearch final {
    QString search;
    int page{1};
    int pageSize{20};
};

struct UserPage final {
    std::vector<ManagedUser> items;
    qint64 total{};
    int page{1};
    int pageSize{20};
};

struct CreateUserInput final {
    QString username;
    QString displayName;
    UserRole role{UserRole::Viewer};
    QString temporaryPassword;
};

struct UpdateUserInput final {
    quint64 id{};
    QString displayName;
    UserRole role{UserRole::Viewer};
    int revision{};
    quint64 actorUserId{};
};

struct SetUserEnabledInput final {
    quint64 id{};
    bool enabled{};
    int revision{};
    quint64 actorUserId{};
};

struct ResetUserPasswordInput final {
    quint64 id{};
    int revision{};
    QString temporaryPassword;
};

struct UserManagementResult final {
    UserManagementError error{UserManagementError::None};
    QString message;
    ManagedUser user;
    UserPage page;

    [[nodiscard]] bool ok() const noexcept {
        return error == UserManagementError::None;
    }
};

class UserManagementRepository {
public:
    virtual ~UserManagementRepository() = default;
    virtual UserManagementResult listUsers(const UserSearch& query) = 0;
    virtual UserManagementResult createUser(
        const CreateUserInput& input,
        const PasswordCredential& credential
    ) = 0;
    virtual UserManagementResult updateUser(const UpdateUserInput& input) = 0;
    virtual UserManagementResult setUserEnabled(
        const SetUserEnabledInput& input
    ) = 0;
    virtual UserManagementResult resetUserPassword(
        const ResetUserPasswordInput& input,
        const PasswordCredential& credential
    ) = 0;
};

class UserManagementService final {
public:
    explicit UserManagementService(
        std::shared_ptr<UserManagementRepository> repository,
        PasswordHasher passwordHasher = PasswordHasher{}
    );

    UserManagementResult listUsers(UserSearch query) const;
    UserManagementResult createUser(CreateUserInput input) const;
    UserManagementResult updateUser(UpdateUserInput input) const;
    UserManagementResult setUserEnabled(SetUserEnabledInput input) const;
    UserManagementResult resetUserPassword(ResetUserPasswordInput input) const;

    static bool validUsername(const QString& username);

private:
    static UserManagementResult failure(
        UserManagementError error,
        const QString& message
    );
    std::shared_ptr<UserManagementRepository> repository_;
    PasswordHasher passwordHasher_;
};

} // namespace manage::auth
