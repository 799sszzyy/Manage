#include "manage/data/database_connection.h"

#include <QSqlError>
#include <QUuid>

#include <utility>

namespace manage::data {

DatabaseConnection::DatabaseConnection(DatabaseConfig config)
    : config_(std::move(config)),
      connectionName_(QStringLiteral("manage-%1").arg(
          QUuid::createUuid().toString(QUuid::WithoutBraces)
      )) {}

DatabaseConnection::~DatabaseConnection() {
    if (!database_.isValid()) {
        return;
    }

    database_.close();
    database_ = {};
    QSqlDatabase::removeDatabase(connectionName_);
}

bool DatabaseConnection::open(QString* errorMessage) {
    lastError_.clear();

    const auto configError = config_.validationError();
    if (!configError.isEmpty()) {
        lastError_ = configError;
    } else if (!mysqlDriverAvailable()) {
        lastError_ = QStringLiteral(
            "QMYSQL is not installed; build or install the Qt MySQL driver"
        );
    } else {
        database_ = QSqlDatabase::addDatabase(
            QStringLiteral("QMYSQL"),
            connectionName_
        );
        database_.setHostName(config_.host);
        database_.setPort(config_.port);
        database_.setDatabaseName(config_.databaseName);
        database_.setUserName(config_.userName);
        database_.setPassword(config_.password);

        if (!database_.open()) {
            lastError_ = database_.lastError().text();
        }
    }

    if (errorMessage != nullptr) {
        *errorMessage = lastError_;
    }
    return lastError_.isEmpty() && database_.isOpen();
}

bool DatabaseConnection::isOpen() const {
    return database_.isOpen();
}

QSqlDatabase DatabaseConnection::database() const {
    return database_;
}

QString DatabaseConnection::lastError() const {
    return lastError_;
}

bool DatabaseConnection::mysqlDriverAvailable() {
    return QSqlDatabase::isDriverAvailable(QStringLiteral("QMYSQL"));
}

QStringList DatabaseConnection::availableDrivers() {
    return QSqlDatabase::drivers();
}

} // namespace manage::data
