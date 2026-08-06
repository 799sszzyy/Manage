#include "manage/data/migration_runner.h"

#include <QCryptographicHash>
#include <QFile>
#include <QMap>
#include <QResource>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <algorithm>
#include <utility>

static void initializeMigrationResources() {
    Q_INIT_RESOURCE(manage_migrations);
}

namespace manage::data {
namespace {

constexpr auto kMigrationLockName = "manage_schema_migrations";

struct AppliedMigration final {
    QString name;
    QByteArray checksum;
    bool dirty{};
};

void setError(QString* output, const QString& message) {
    if (output != nullptr) {
        *output = message;
    }
}

QString queryError(const QString& operation, const QSqlQuery& query) {
    return QStringLiteral("%1: %2").arg(operation, query.lastError().text());
}

class AdvisoryLock final {
public:
    explicit AdvisoryLock(QSqlDatabase database)
        : database_(std::move(database)) {}

    AdvisoryLock(const AdvisoryLock&) = delete;
    AdvisoryLock& operator=(const AdvisoryLock&) = delete;

    ~AdvisoryLock() {
        if (!acquired_ || !database_.isOpen()) {
            return;
        }

        QSqlQuery query(database_);
        query.prepare(QStringLiteral("SELECT RELEASE_LOCK(?)"));
        query.addBindValue(QString::fromLatin1(kMigrationLockName));
        query.exec();
    }

    bool acquire(QString* errorMessage) {
        QSqlQuery query(database_);
        query.prepare(QStringLiteral("SELECT GET_LOCK(?, 10)"));
        query.addBindValue(QString::fromLatin1(kMigrationLockName));
        if (!query.exec()) {
            setError(
                errorMessage,
                queryError(QStringLiteral("unable to acquire migration lock"), query)
            );
            return false;
        }
        if (!query.next() || query.value(0).toInt() != 1) {
            setError(
                errorMessage,
                QStringLiteral("timed out waiting for the database migration lock")
            );
            return false;
        }

        acquired_ = true;
        return true;
    }

private:
    QSqlDatabase database_;
    bool acquired_{};
};

bool createMigrationTable(QSqlDatabase database, QString* errorMessage) {
    QSqlQuery query(database);
    const auto sql = QStringLiteral(R"SQL(
        CREATE TABLE IF NOT EXISTS schema_migrations (
            version INT UNSIGNED NOT NULL,
            name VARCHAR(255) NOT NULL,
            checksum CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
            dirty BOOLEAN NOT NULL DEFAULT TRUE,
            started_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
            applied_at DATETIME(6) NULL,
            PRIMARY KEY (version)
        ) ENGINE=InnoDB DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci
    )SQL");
    if (!query.exec(sql)) {
        setError(
            errorMessage,
            queryError(QStringLiteral("unable to create schema_migrations"), query)
        );
        return false;
    }
    return true;
}

bool readAppliedMigrations(
    QSqlDatabase database,
    QMap<int, AppliedMigration>& applied,
    QString* errorMessage
) {
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral(
            "SELECT version, name, checksum, dirty "
            "FROM schema_migrations ORDER BY version"
        ))) {
        setError(
            errorMessage,
            queryError(QStringLiteral("unable to read schema_migrations"), query)
        );
        return false;
    }

    while (query.next()) {
        const auto version = query.value(0).toInt();
        applied.insert(
            version,
            AppliedMigration{
                query.value(1).toString(),
                query.value(2).toByteArray().toLower(),
                query.value(3).toBool(),
            }
        );
    }
    return true;
}

bool validateAppliedMigrations(
    const QList<Migration>& catalog,
    const QMap<int, AppliedMigration>& applied,
    QString* errorMessage
) {
    QMap<int, Migration> expected;
    for (const auto& migration : catalog) {
        if (migration.version <= 0 || expected.contains(migration.version)) {
            setError(
                errorMessage,
                QStringLiteral("built-in migration versions must be positive and unique")
            );
            return false;
        }
        expected.insert(migration.version, migration);
    }

    for (auto iterator = applied.cbegin(); iterator != applied.cend(); ++iterator) {
        const auto version = iterator.key();
        const auto& actual = iterator.value();
        if (!expected.contains(version)) {
            setError(
                errorMessage,
                QStringLiteral("database contains unknown migration version %1")
                    .arg(version)
            );
            return false;
        }
        if (actual.dirty) {
            setError(
                errorMessage,
                QStringLiteral(
                    "migration %1 is marked dirty; inspect the database before retrying"
                ).arg(version)
            );
            return false;
        }

        const auto& migration = expected.value(version);
        if (actual.name != migration.name || actual.checksum != migration.checksum) {
            setError(
                errorMessage,
                QStringLiteral(
                    "migration %1 differs from the version already applied"
                ).arg(version)
            );
            return false;
        }
    }
    return true;
}

bool insertDirtyMigration(
    QSqlDatabase database,
    const Migration& migration,
    QString* errorMessage
) {
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "INSERT INTO schema_migrations "
        "(version, name, checksum, dirty, started_at, applied_at) "
        "VALUES (?, ?, ?, TRUE, CURRENT_TIMESTAMP(6), NULL)"
    ));
    query.addBindValue(migration.version);
    query.addBindValue(migration.name);
    query.addBindValue(QString::fromLatin1(migration.checksum));
    if (!query.exec()) {
        setError(
            errorMessage,
            queryError(
                QStringLiteral("unable to mark migration %1 as started")
                    .arg(migration.version),
                query
            )
        );
        return false;
    }
    return true;
}

bool markMigrationApplied(
    QSqlDatabase database,
    int version,
    QString* errorMessage
) {
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "UPDATE schema_migrations "
        "SET dirty = FALSE, applied_at = CURRENT_TIMESTAMP(6) "
        "WHERE version = ? AND dirty = TRUE"
    ));
    query.addBindValue(version);
    if (!query.exec() || query.numRowsAffected() != 1) {
        setError(
            errorMessage,
            query.lastError().isValid()
                ? queryError(
                      QStringLiteral("unable to finish migration %1").arg(version),
                      query
                  )
                : QStringLiteral("migration %1 could not be marked complete")
                      .arg(version)
        );
        return false;
    }
    return true;
}

bool executeMigration(
    QSqlDatabase database,
    const Migration& migration,
    const QStringList& statements,
    QString* errorMessage
) {
    if (!insertDirtyMigration(database, migration, errorMessage)) {
        return false;
    }

    for (qsizetype index = 0; index < statements.size(); ++index) {
        QSqlQuery query(database);
        if (query.exec(statements.at(index))) {
            continue;
        }

        auto preview = statements.at(index).simplified();
        if (preview.size() > 160) {
            preview = preview.left(157) + QStringLiteral("...");
        }
        setError(
            errorMessage,
            QStringLiteral("migration %1 statement %2 failed (%3): %4")
                .arg(migration.version)
                .arg(index + 1)
                .arg(preview)
                .arg(query.lastError().text())
        );
        return false;
    }

    return markMigrationApplied(database, migration.version, errorMessage);
}

} // namespace

MigrationRunner::MigrationRunner(QSqlDatabase database)
    : database_(std::move(database)) {}

bool MigrationRunner::migrate(
    MigrationReport* report,
    QString* errorMessage
) {
    if (report != nullptr) {
        *report = {};
    }
    setError(errorMessage, {});

    if (!database_.isValid() || !database_.isOpen()) {
        setError(errorMessage, QStringLiteral("database connection is not open"));
        return false;
    }

    const auto driverName = database_.driverName();
    if (driverName != QStringLiteral("QMYSQL") &&
        driverName != QStringLiteral("QMARIADB")) {
        setError(
            errorMessage,
            QStringLiteral("database migrations require QMYSQL or QMARIADB")
        );
        return false;
    }

    QString catalogError;
    auto catalog = builtInMigrations(&catalogError);
    if (!catalogError.isEmpty()) {
        setError(errorMessage, catalogError);
        return false;
    }
    std::sort(catalog.begin(), catalog.end(), [](const auto& left, const auto& right) {
        return left.version < right.version;
    });

    QMap<int, QStringList> parsedStatements;
    for (const auto& migration : catalog) {
        QString parseError;
        const auto statements = splitSqlStatements(migration.sql, &parseError);
        if (!parseError.isEmpty() || statements.isEmpty()) {
            setError(
                errorMessage,
                parseError.isEmpty()
                    ? QStringLiteral("migration %1 contains no SQL statements")
                          .arg(migration.version)
                    : QStringLiteral("migration %1 is invalid: %2")
                          .arg(migration.version)
                          .arg(parseError)
            );
            return false;
        }
        parsedStatements.insert(migration.version, statements);
    }

    // The advisory lock prevents two server processes from applying the same
    // schema change concurrently. Stored checksums below also reject edited
    // historical migrations instead of silently accepting schema drift.
    AdvisoryLock lock(database_);
    if (!lock.acquire(errorMessage) ||
        !createMigrationTable(database_, errorMessage)) {
        return false;
    }

    QMap<int, AppliedMigration> applied;
    if (!readAppliedMigrations(database_, applied, errorMessage) ||
        !validateAppliedMigrations(catalog, applied, errorMessage)) {
        return false;
    }

    const auto previousVersion = applied.isEmpty() ? 0 : applied.lastKey();
    auto currentVersion = previousVersion;
    auto appliedCount = 0;
    for (const auto& migration : catalog) {
        if (applied.contains(migration.version)) {
            continue;
        }
        if (!executeMigration(
                database_,
                migration,
                parsedStatements.value(migration.version),
                errorMessage
            )) {
            return false;
        }
        currentVersion = migration.version;
        ++appliedCount;
    }

    if (report != nullptr) {
        report->previousVersion = previousVersion;
        report->currentVersion = currentVersion;
        report->appliedCount = appliedCount;
    }
    return true;
}

QList<Migration> MigrationRunner::builtInMigrations(QString* errorMessage) {
    initializeMigrationResources();
    setError(errorMessage, {});

    struct ResourceMigration final {
        int version;
        const char* name;
    };
    constexpr ResourceMigration resources[] = {
        {1, "001_initial_schema"},
        {2, "002_quote_bom_template"},
        {3, "003_quote_bom_quantity"},
    };

    QList<Migration> migrations;
    for (const auto& resource : resources) {
        const auto name = QString::fromLatin1(resource.name);
        QFile file(QStringLiteral(":/manage/migrations/%1.sql").arg(name));
        if (!file.open(QIODevice::ReadOnly)) {
            setError(
                errorMessage,
                QStringLiteral("unable to open built-in migration %1").arg(name)
            );
            return {};
        }

        const auto bytes = file.readAll();
        if (bytes.trimmed().isEmpty()) {
            setError(
                errorMessage,
                QStringLiteral("built-in migration %1 is empty").arg(name)
            );
            return {};
        }
        migrations.append(Migration{
            resource.version,
            name,
            QString::fromUtf8(bytes),
            QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex(),
        });
    }
    return migrations;
}

QStringList MigrationRunner::splitSqlStatements(
    const QString& sql,
    QString* errorMessage
) {
    enum class State {
        Normal,
        SingleQuoted,
        DoubleQuoted,
        BacktickQuoted,
        LineComment,
        BlockComment,
    };

    setError(errorMessage, {});
    QStringList statements;
    QString current;
    auto state = State::Normal;

    for (qsizetype index = 0; index < sql.size(); ++index) {
        const auto character = sql.at(index);
        const auto next = index + 1 < sql.size() ? sql.at(index + 1) : QChar{};

        if (state == State::LineComment) {
            if (character == QLatin1Char('\n')) {
                current.append(character);
                state = State::Normal;
            }
            continue;
        }
        if (state == State::BlockComment) {
            if (character == QLatin1Char('*') && next == QLatin1Char('/')) {
                current.append(QLatin1Char(' '));
                ++index;
                state = State::Normal;
            }
            continue;
        }

        if (state != State::Normal) {
            current.append(character);
            const auto delimiter = state == State::SingleQuoted
                                       ? QLatin1Char('\'')
                                       : state == State::DoubleQuoted
                                           ? QLatin1Char('"')
                                           : QLatin1Char('`');
            if (character == QLatin1Char('\\') && index + 1 < sql.size()) {
                current.append(sql.at(++index));
            } else if (character == delimiter) {
                if (next == delimiter) {
                    current.append(sql.at(++index));
                } else {
                    state = State::Normal;
                }
            }
            continue;
        }

        if (character == QLatin1Char('-') && next == QLatin1Char('-') &&
            (index + 2 >= sql.size() || sql.at(index + 2).isSpace())) {
            ++index;
            state = State::LineComment;
        } else if (character == QLatin1Char('#')) {
            state = State::LineComment;
        } else if (character == QLatin1Char('/') && next == QLatin1Char('*')) {
            ++index;
            state = State::BlockComment;
        } else if (character == QLatin1Char('\'')) {
            current.append(character);
            state = State::SingleQuoted;
        } else if (character == QLatin1Char('"')) {
            current.append(character);
            state = State::DoubleQuoted;
        } else if (character == QLatin1Char('`')) {
            current.append(character);
            state = State::BacktickQuoted;
        } else if (character == QLatin1Char(';')) {
            const auto statement = current.trimmed();
            if (!statement.isEmpty()) {
                statements.append(statement);
            }
            current.clear();
        } else {
            current.append(character);
        }
    }

    if (state == State::SingleQuoted || state == State::DoubleQuoted ||
        state == State::BacktickQuoted || state == State::BlockComment) {
        setError(errorMessage, QStringLiteral("unterminated SQL quote or comment"));
        return {};
    }

    const auto trailing = current.trimmed();
    if (!trailing.isEmpty()) {
        statements.append(trailing);
    }
    return statements;
}

} // namespace manage::data
