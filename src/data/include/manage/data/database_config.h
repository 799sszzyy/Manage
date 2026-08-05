#pragma once

#include <QString>

namespace manage::data {

struct DatabaseConfig final {
    QString host{QStringLiteral("127.0.0.1")};
    int port{3306};
    QString databaseName{QStringLiteral("manage")};
    QString userName{QStringLiteral("manage_app")};
    QString password;

    static DatabaseConfig fromEnvironment();
    QString validationError() const;
};

} // namespace manage::data
