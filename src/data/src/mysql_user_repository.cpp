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

} // namespace manage::data
