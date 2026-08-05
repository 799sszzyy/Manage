#pragma once

#include "manage/data/database_config.h"

#include <QSqlDatabase>
#include <QString>
#include <QStringList>

namespace manage::data {

class DatabaseConnection final {
public:
    explicit DatabaseConnection(DatabaseConfig config);
    ~DatabaseConnection();

    DatabaseConnection(const DatabaseConnection&) = delete;
    DatabaseConnection& operator=(const DatabaseConnection&) = delete;

    bool open(QString* errorMessage = nullptr);
    bool isOpen() const;
    QSqlDatabase database() const;
    QString lastError() const;

    static bool mysqlDriverAvailable();
    static QStringList availableDrivers();

private:
    DatabaseConfig config_;
    QString connectionName_;
    QSqlDatabase database_;
    QString lastError_;
};

} // namespace manage::data
