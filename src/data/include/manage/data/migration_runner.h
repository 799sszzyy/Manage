#pragma once

#include <QByteArray>
#include <QList>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>

namespace manage::data {

struct Migration final {
    int version{};
    QString name;
    QString sql;
    QByteArray checksum;
};

struct MigrationReport final {
    int previousVersion{};
    int currentVersion{};
    int appliedCount{};
};

class MigrationRunner final {
public:
    explicit MigrationRunner(QSqlDatabase database);

    bool migrate(
        MigrationReport* report = nullptr,
        QString* errorMessage = nullptr
    );

    static QList<Migration> builtInMigrations(
        QString* errorMessage = nullptr
    );
    static QStringList splitSqlStatements(
        const QString& sql,
        QString* errorMessage = nullptr
    );

private:
    QSqlDatabase database_;
};

} // namespace manage::data
