#include "manage/data/mysql_user_repository.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <utility>

namespace manage::data {
namespace {

using manage::auth::RepositoryResult;
using manage::auth::UserAccount;
using manage::auth::UserRole;

const auto kUserColumns = QStringLiteral(
    "SELECT u.id, u.username, u.display_name, r.code, "
    "u.password_algorithm, u.password_hash, u.password_salt, "
    "u.password_iterations, u.must_change_password, u.is_enabled "
    "FROM users u JOIN roles r ON r.id = u.role_id "
);

const auto kManagedUserColumns = QStringLiteral(
    "SELECT u.id, u.username, u.display_name, r.code, u.is_enabled, "
    "u.must_change_password, u.revision, u.created_at, u.updated_at "
    "FROM users u JOIN roles r ON r.id = u.role_id "
);

void setError(QString* target, const QString& message) {
    if (target != nullptr) {
        *target = message;
    }
}

RepositoryResult databaseFailure(
    QString* errorMessage,
    const QString& context,
    const QSqlQuery& query
) {
    setError(
        errorMessage,
        QStringLiteral("%1: %2").arg(context, query.lastError().text())
    );
    return RepositoryResult::Failure;
}

bool parseRole(const QString& code, UserRole* role) {
    if (code == QStringLiteral("admin")) {
        *role = UserRole::Admin;
        return true;
    }
    if (code == QStringLiteral("quoter")) {
        *role = UserRole::Quoter;
        return true;
    }
    if (code == QStringLiteral("viewer")) {
        *role = UserRole::Viewer;
        return true;
    }
    return false;
}

QString roleValue(UserRole role) {
    return manage::auth::roleCode(role);
}

manage::auth::UserManagementResult managementFailure(
    manage::auth::UserManagementError error,
    const QString& message
) {
    manage::auth::UserManagementResult result;
    result.error = error;
    result.message = message;
    return result;
}

bool readManagedUser(
    const QSqlQuery& query,
    manage::auth::ManagedUser* user,
    QString* errorMessage = nullptr
) {
    UserRole role;
    if (!parseRole(query.value(3).toString(), &role)) {
        setError(errorMessage, QStringLiteral("account contains an unknown role"));
        return false;
    }
    *user = {
        query.value(0).toULongLong(),
        query.value(1).toString(),
        query.value(2).toString(),
        role,
        query.value(4).toBool(),
        query.value(5).toBool(),
        query.value(6).toInt(),
        query.value(7).toDateTime(),
        query.value(8).toDateTime(),
    };
    return true;
}

manage::auth::UserManagementResult managedUserById(
    QSqlDatabase database,
    quint64 id
) {
    QSqlQuery query(database);
    query.prepare(kManagedUserColumns + QStringLiteral("WHERE u.id = ? LIMIT 1"));
    query.addBindValue(QVariant::fromValue(id));
    if (!query.exec()) {
        return managementFailure(
            manage::auth::UserManagementError::RepositoryFailure,
            query.lastError().text()
        );
    }
    if (!query.next()) {
        return managementFailure(
            manage::auth::UserManagementError::NotFound,
            QStringLiteral("user was not found")
        );
    }
    manage::auth::UserManagementResult result;
    if (!readManagedUser(query, &result.user, &result.message)) {
        result.error = manage::auth::UserManagementError::RepositoryFailure;
    }
    return result;
}

bool beginTransaction(QSqlDatabase& database, QString* message) {
    if (!database.isValid() || !database.isOpen()) {
        *message = QStringLiteral("database connection is not open");
        return false;
    }
    if (!database.transaction()) {
        *message = database.lastError().text();
        return false;
    }
    return true;
}

manage::auth::UserManagementResult lockedManagedUser(
    QSqlDatabase database,
    quint64 id
) {
    QSqlQuery query(database);
    query.prepare(kManagedUserColumns + QStringLiteral("WHERE u.id = ? LIMIT 1 FOR UPDATE"));
    query.addBindValue(QVariant::fromValue(id));
    if (!query.exec()) {
        return managementFailure(manage::auth::UserManagementError::RepositoryFailure,
                                 query.lastError().text());
    }
    if (!query.next()) {
        return managementFailure(manage::auth::UserManagementError::NotFound,
                                 QStringLiteral("user was not found"));
    }
    manage::auth::UserManagementResult result;
    if (!readManagedUser(query, &result.user, &result.message)) {
        result.error = manage::auth::UserManagementError::RepositoryFailure;
    }
    return result;
}

bool isLastEnabledAdministrator(QSqlDatabase database, QString* errorMessage) {
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral(
            "SELECT u.id FROM users u JOIN roles r ON r.id = u.role_id "
            "WHERE r.code = 'admin' AND u.is_enabled = TRUE FOR UPDATE"
        ))) {
        *errorMessage = query.lastError().text();
        return false;
    }
    int count = 0;
    while (query.next()) {
        ++count;
    }
    return count <= 1;
}

bool readAccount(const QSqlQuery& query, UserAccount* account, QString* errorMessage) {
    if (account == nullptr) {
        setError(errorMessage, QStringLiteral("account output must not be null"));
        return false;
    }

    UserRole role;
    if (!parseRole(query.value(3).toString(), &role)) {
        setError(errorMessage, QStringLiteral("account contains an unknown role"));
        return false;
    }
    *account = {
        query.value(0).toULongLong(),
        query.value(1).toString(),
        query.value(2).toString(),
        role,
        {
            query.value(4).toString(),
            query.value(5).toByteArray(),
            query.value(6).toByteArray(),
            query.value(7).toInt(),
        },
        query.value(8).toBool(),
        query.value(9).toBool(),
    };
    return true;
}

RepositoryResult readSingleAccount(
    QSqlQuery& query,
    UserAccount* account,
    QString* errorMessage,
    const QString& context
) {
    if (!query.exec()) {
        return databaseFailure(errorMessage, context, query);
    }
    if (!query.next()) {
        setError(errorMessage, {});
        return RepositoryResult::NotFound;
    }
    if (!readAccount(query, account, errorMessage)) {
        return RepositoryResult::Failure;
    }
    setError(errorMessage, {});
    return RepositoryResult::Success;
}

} // namespace

MySqlUserRepository::MySqlUserRepository(QSqlDatabase database)
    : database_(std::move(database)) {}

RepositoryResult MySqlUserRepository::findByUsername(
    const QString& username,
    UserAccount* account,
    QString* errorMessage
) {
    QSqlQuery query(database_);
    query.prepare(kUserColumns + QStringLiteral("WHERE u.username = ? LIMIT 1"));
    query.addBindValue(username);
    return readSingleAccount(
        query,
        account,
        errorMessage,
        QStringLiteral("unable to find account by username")
    );
}

RepositoryResult MySqlUserRepository::findById(
    quint64 id,
    UserAccount* account,
    QString* errorMessage
) {
    QSqlQuery query(database_);
    query.prepare(kUserColumns + QStringLiteral("WHERE u.id = ? LIMIT 1"));
    query.addBindValue(QVariant::fromValue(id));
    return readSingleAccount(
        query,
        account,
        errorMessage,
        QStringLiteral("unable to find account by id")
    );
}

RepositoryResult MySqlUserRepository::bootstrapAdministrator(
    const QString& displayName,
    const manage::auth::PasswordCredential& credential,
    UserAccount* account,
    QString* errorMessage
) {
    if (!database_.isValid() || !database_.isOpen()) {
        setError(errorMessage, QStringLiteral("database connection is not open"));
        return RepositoryResult::Failure;
    }
    if (!database_.transaction()) {
        setError(
            errorMessage,
            QStringLiteral("unable to start bootstrap transaction: %1")
                .arg(database_.lastError().text())
        );
        return RepositoryResult::Failure;
    }

    QSqlQuery select(database_);
    select.prepare(
        kUserColumns +
        QStringLiteral(
            "WHERE r.code = 'admin' ORDER BY u.id LIMIT 1 FOR UPDATE"
        )
    );
    UserAccount existing;
    const auto selectResult = readSingleAccount(
        select,
        &existing,
        errorMessage,
        QStringLiteral("unable to lock the initial administrator")
    );
    if (selectResult == RepositoryResult::NotFound) {
        database_.rollback();
        setError(errorMessage, QStringLiteral("initial administrator record is missing"));
        return RepositoryResult::Failure;
    }
    if (selectResult != RepositoryResult::Success) {
        database_.rollback();
        return selectResult;
    }
    if (existing.enabled || !existing.credential.algorithm.isEmpty() ||
        !existing.credential.hash.isEmpty()) {
        database_.rollback();
        setError(errorMessage, {});
        return RepositoryResult::Conflict;
    }

    QSqlQuery update(database_);
    update.prepare(QStringLiteral(
        "UPDATE users SET display_name = ?, password_algorithm = ?, "
        "password_hash = ?, password_salt = ?, password_iterations = ?, "
        "must_change_password = TRUE, is_enabled = TRUE, revision = revision + 1 "
        "WHERE id = ? AND is_enabled = FALSE AND password_hash IS NULL"
    ));
    update.addBindValue(displayName);
    update.addBindValue(credential.algorithm);
    update.addBindValue(credential.hash);
    update.addBindValue(credential.salt);
    update.addBindValue(credential.iterations);
    update.addBindValue(QVariant::fromValue(existing.id));
    if (!update.exec()) {
        database_.rollback();
        return databaseFailure(
            errorMessage,
            QStringLiteral("unable to configure the initial administrator"),
            update
        );
    }
    if (update.numRowsAffected() != 1) {
        database_.rollback();
        setError(errorMessage, {});
        return RepositoryResult::Conflict;
    }
    if (!database_.commit()) {
        database_.rollback();
        setError(
            errorMessage,
            QStringLiteral("unable to commit bootstrap transaction: %1")
                .arg(database_.lastError().text())
        );
        return RepositoryResult::Failure;
    }

    existing.displayName = displayName;
    existing.credential = credential;
    existing.mustChangePassword = true;
    existing.enabled = true;
    if (account != nullptr) {
        *account = std::move(existing);
    }
    setError(errorMessage, {});
    return RepositoryResult::Success;
}

RepositoryResult MySqlUserRepository::changePassword(
    quint64 userId,
    const QByteArray& expectedPasswordHash,
    const manage::auth::PasswordCredential& credential,
    UserAccount* account,
    QString* errorMessage
) {
    if (!database_.isValid() || !database_.isOpen()) {
        setError(errorMessage, QStringLiteral("database connection is not open"));
        return RepositoryResult::Failure;
    }
    if (!database_.transaction()) {
        setError(
            errorMessage,
            QStringLiteral("unable to start password transaction: %1")
                .arg(database_.lastError().text())
        );
        return RepositoryResult::Failure;
    }

    QSqlQuery select(database_);
    select.prepare(
        kUserColumns + QStringLiteral("WHERE u.id = ? LIMIT 1 FOR UPDATE")
    );
    select.addBindValue(QVariant::fromValue(userId));
    UserAccount existing;
    const auto selectResult = readSingleAccount(
        select,
        &existing,
        errorMessage,
        QStringLiteral("unable to lock the account for password change")
    );
    if (selectResult != RepositoryResult::Success) {
        database_.rollback();
        return selectResult;
    }
    if (!existing.enabled || existing.credential.hash != expectedPasswordHash) {
        database_.rollback();
        setError(errorMessage, {});
        return RepositoryResult::Conflict;
    }

    QSqlQuery update(database_);
    update.prepare(QStringLiteral(
        "UPDATE users SET password_algorithm = ?, password_hash = ?, "
        "password_salt = ?, password_iterations = ?, "
        "must_change_password = FALSE, revision = revision + 1 "
        "WHERE id = ? AND is_enabled = TRUE AND password_hash = ?"
    ));
    update.addBindValue(credential.algorithm);
    update.addBindValue(credential.hash);
    update.addBindValue(credential.salt);
    update.addBindValue(credential.iterations);
    update.addBindValue(QVariant::fromValue(userId));
    update.addBindValue(expectedPasswordHash);
    if (!update.exec()) {
        database_.rollback();
        return databaseFailure(
            errorMessage,
            QStringLiteral("unable to update the password"),
            update
        );
    }
    if (update.numRowsAffected() != 1) {
        database_.rollback();
        setError(errorMessage, {});
        return RepositoryResult::Conflict;
    }
    if (!database_.commit()) {
        database_.rollback();
        setError(
            errorMessage,
            QStringLiteral("unable to commit password transaction: %1")
                .arg(database_.lastError().text())
        );
        return RepositoryResult::Failure;
    }

    existing.credential = credential;
    existing.mustChangePassword = false;
    if (account != nullptr) {
        *account = std::move(existing);
    }
    setError(errorMessage, {});
    return RepositoryResult::Success;
}

manage::auth::UserManagementResult MySqlUserRepository::listUsers(
    const manage::auth::UserSearch& search
) {
    if (!database_.isValid() || !database_.isOpen()) {
        return managementFailure(
            manage::auth::UserManagementError::RepositoryFailure,
            QStringLiteral("database connection is not open")
        );
    }
    const auto like = QStringLiteral("%") + search.search + QStringLiteral("%");
    QSqlQuery count(database_);
    count.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM users WHERE (? = '' OR username LIKE ? "
        "OR display_name LIKE ?)"
    ));
    count.addBindValue(search.search);
    count.addBindValue(like);
    count.addBindValue(like);
    if (!count.exec() || !count.next()) {
        return managementFailure(
            manage::auth::UserManagementError::RepositoryFailure,
            count.lastError().text()
        );
    }

    QSqlQuery query(database_);
    query.prepare(
        kManagedUserColumns + QStringLiteral(
            "WHERE (? = '' OR u.username LIKE ? OR u.display_name LIKE ?) "
            "ORDER BY u.id LIMIT ? OFFSET ?"
        )
    );
    query.addBindValue(search.search);
    query.addBindValue(like);
    query.addBindValue(like);
    query.addBindValue(search.pageSize);
    query.addBindValue((search.page - 1) * search.pageSize);
    if (!query.exec()) {
        return managementFailure(
            manage::auth::UserManagementError::RepositoryFailure,
            query.lastError().text()
        );
    }

    manage::auth::UserManagementResult result;
    result.page.total = count.value(0).toLongLong();
    result.page.page = search.page;
    result.page.pageSize = search.pageSize;
    while (query.next()) {
        manage::auth::ManagedUser user;
        if (!readManagedUser(query, &user, &result.message)) {
            result.error = manage::auth::UserManagementError::RepositoryFailure;
            return result;
        }
        result.page.items.push_back(std::move(user));
    }
    return result;
}

manage::auth::UserManagementResult MySqlUserRepository::createUser(
    const manage::auth::CreateUserInput& input,
    const manage::auth::PasswordCredential& credential
) {
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO users (username, display_name, role_id, password_algorithm, "
        "password_hash, password_salt, password_iterations, "
        "must_change_password, is_enabled) "
        "SELECT ?, ?, id, ?, ?, ?, ?, TRUE, TRUE FROM roles WHERE code = ?"
    ));
    query.addBindValue(input.username);
    query.addBindValue(input.displayName);
    query.addBindValue(credential.algorithm);
    query.addBindValue(credential.hash);
    query.addBindValue(credential.salt);
    query.addBindValue(credential.iterations);
    query.addBindValue(roleValue(input.role));
    if (!query.exec()) {
        const auto duplicate = query.lastError().nativeErrorCode() == QStringLiteral("1062");
        return managementFailure(
            duplicate ? manage::auth::UserManagementError::Conflict
                      : manage::auth::UserManagementError::RepositoryFailure,
            duplicate ? QStringLiteral("username already exists")
                      : query.lastError().text()
        );
    }
    if (query.numRowsAffected() != 1) {
        return managementFailure(manage::auth::UserManagementError::Validation,
                                 QStringLiteral("role is invalid"));
    }
    return managedUserById(database_, query.lastInsertId().toULongLong());
}

manage::auth::UserManagementResult MySqlUserRepository::updateUser(
    const manage::auth::UpdateUserInput& input
) {
    QString transactionError;
    if (!beginTransaction(database_, &transactionError)) {
        return managementFailure(manage::auth::UserManagementError::RepositoryFailure,
                                 transactionError);
    }
    auto existing = lockedManagedUser(database_, input.id);
    if (!existing.ok()) {
        database_.rollback();
        return existing;
    }
    if (existing.user.revision != input.revision) {
        database_.rollback();
        return managementFailure(manage::auth::UserManagementError::Conflict,
                                 QStringLiteral("user revision has changed"));
    }
    if (existing.user.enabled && existing.user.role == UserRole::Admin &&
        input.role != UserRole::Admin) {
        QString lockError;
        const auto last = isLastEnabledAdministrator(database_, &lockError);
        if (!lockError.isEmpty()) {
            database_.rollback();
            return managementFailure(manage::auth::UserManagementError::RepositoryFailure,
                                     lockError);
        }
        if (last) {
            database_.rollback();
            return managementFailure(manage::auth::UserManagementError::ProtectedAccount,
                                     QStringLiteral("the last enabled administrator cannot be downgraded"));
        }
    }

    QSqlQuery update(database_);
    update.prepare(QStringLiteral(
        "UPDATE users u JOIN roles r ON r.code = ? "
        "SET u.display_name = ?, u.role_id = r.id, u.revision = u.revision + 1 "
        "WHERE u.id = ? AND u.revision = ?"
    ));
    update.addBindValue(roleValue(input.role));
    update.addBindValue(input.displayName);
    update.addBindValue(QVariant::fromValue(input.id));
    update.addBindValue(input.revision);
    if (!update.exec()) {
        database_.rollback();
        return managementFailure(manage::auth::UserManagementError::RepositoryFailure,
                                 update.lastError().text());
    }
    if (update.numRowsAffected() != 1) {
        database_.rollback();
        return managementFailure(manage::auth::UserManagementError::Conflict,
                                 QStringLiteral("user revision has changed or role is invalid"));
    }
    if (!database_.commit()) {
        database_.rollback();
        return managementFailure(manage::auth::UserManagementError::RepositoryFailure,
                                 database_.lastError().text());
    }
    return managedUserById(database_, input.id);
}

manage::auth::UserManagementResult MySqlUserRepository::setUserEnabled(
    const manage::auth::SetUserEnabledInput& input
) {
    if (!input.enabled && input.id == input.actorUserId) {
        return managementFailure(manage::auth::UserManagementError::ProtectedAccount,
                                 QStringLiteral("the current administrator cannot be disabled"));
    }
    QString transactionError;
    if (!beginTransaction(database_, &transactionError)) {
        return managementFailure(manage::auth::UserManagementError::RepositoryFailure,
                                 transactionError);
    }
    auto existing = lockedManagedUser(database_, input.id);
    if (!existing.ok()) {
        database_.rollback();
        return existing;
    }
    if (existing.user.revision != input.revision) {
        database_.rollback();
        return managementFailure(manage::auth::UserManagementError::Conflict,
                                 QStringLiteral("user revision has changed"));
    }
    if (!input.enabled && existing.user.enabled &&
        existing.user.role == UserRole::Admin) {
        QString lockError;
        const auto last = isLastEnabledAdministrator(database_, &lockError);
        if (!lockError.isEmpty()) {
            database_.rollback();
            return managementFailure(manage::auth::UserManagementError::RepositoryFailure,
                                     lockError);
        }
        if (last) {
            database_.rollback();
            return managementFailure(manage::auth::UserManagementError::ProtectedAccount,
                                     QStringLiteral("the last enabled administrator cannot be disabled"));
        }
    }
    QSqlQuery update(database_);
    update.prepare(QStringLiteral(
        "UPDATE users SET is_enabled = ?, revision = revision + 1 "
        "WHERE id = ? AND revision = ?"
    ));
    update.addBindValue(input.enabled);
    update.addBindValue(QVariant::fromValue(input.id));
    update.addBindValue(input.revision);
    if (!update.exec() || update.numRowsAffected() != 1) {
        const auto message = update.lastError().isValid()
                                 ? update.lastError().text()
                                 : QStringLiteral("user revision has changed");
        database_.rollback();
        return managementFailure(
            update.lastError().isValid()
                ? manage::auth::UserManagementError::RepositoryFailure
                : manage::auth::UserManagementError::Conflict,
            message
        );
    }
    if (!database_.commit()) {
        database_.rollback();
        return managementFailure(manage::auth::UserManagementError::RepositoryFailure,
                                 database_.lastError().text());
    }
    return managedUserById(database_, input.id);
}

manage::auth::UserManagementResult MySqlUserRepository::resetUserPassword(
    const manage::auth::ResetUserPasswordInput& input,
    const manage::auth::PasswordCredential& credential
) {
    QSqlQuery update(database_);
    update.prepare(QStringLiteral(
        "UPDATE users SET password_algorithm = ?, password_hash = ?, "
        "password_salt = ?, password_iterations = ?, must_change_password = TRUE, "
        "revision = revision + 1 WHERE id = ? AND revision = ?"
    ));
    update.addBindValue(credential.algorithm);
    update.addBindValue(credential.hash);
    update.addBindValue(credential.salt);
    update.addBindValue(credential.iterations);
    update.addBindValue(QVariant::fromValue(input.id));
    update.addBindValue(input.revision);
    if (!update.exec()) {
        return managementFailure(manage::auth::UserManagementError::RepositoryFailure,
                                 update.lastError().text());
    }
    if (update.numRowsAffected() != 1) {
        const auto exists = managedUserById(database_, input.id);
        return exists.error == manage::auth::UserManagementError::NotFound
                   ? exists
                   : managementFailure(manage::auth::UserManagementError::Conflict,
                                       QStringLiteral("user revision has changed"));
    }
    return managedUserById(database_, input.id);
}

} // namespace manage::data
