#include "manage/data/database_config.h"

#include <QtGlobal>

namespace manage::data {

DatabaseConfig DatabaseConfig::fromEnvironment() {
    DatabaseConfig config;
    config.host = qEnvironmentVariable("MANAGE_DB_HOST", config.host);
    config.databaseName = qEnvironmentVariable(
        "MANAGE_DB_NAME",
        config.databaseName
    );
    config.userName = qEnvironmentVariable("MANAGE_DB_USER", config.userName);
    config.password = qEnvironmentVariable("MANAGE_DB_PASSWORD");

    bool portIsValid = false;
    config.port = qEnvironmentVariable("MANAGE_DB_PORT", QStringLiteral("3306"))
                      .toInt(&portIsValid);
    if (!portIsValid) {
        config.port = -1;
    }

    return config;
}

QString DatabaseConfig::validationError() const {
    if (host != QStringLiteral("127.0.0.1") &&
        host.compare(QStringLiteral("localhost"), Qt::CaseInsensitive) != 0) {
        return QStringLiteral("MANAGE_DB_HOST must be 127.0.0.1 or localhost");
    }
    if (port < 1 || port > 65'535) {
        return QStringLiteral("MANAGE_DB_PORT must be between 1 and 65535");
    }
    if (databaseName.trimmed().isEmpty()) {
        return QStringLiteral("MANAGE_DB_NAME must not be empty");
    }
    if (userName.trimmed().isEmpty()) {
        return QStringLiteral("MANAGE_DB_USER must not be empty");
    }
    if (password.isEmpty()) {
        return QStringLiteral("MANAGE_DB_PASSWORD must not be empty");
    }
    return {};
}

} // namespace manage::data
