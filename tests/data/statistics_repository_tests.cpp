#include "manage/data/mysql_statistics_repository.h"

#include <QCoreApplication>
#include <QDate>
#include <QSqlDatabase>

#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    try {
        manage::data::MySqlStatisticsRepository repository(QSqlDatabase{});
        auto result = repository.query({{}, QDate(2026, 1, 1), std::nullopt, std::nullopt});
        require(!result.ok() && result.error == manage::data::QuoteErrorCode::Validation,
                "invalid date must be rejected before database access");
        result = repository.query({QDate(2026, 2, 1), QDate(2026, 1, 1), std::nullopt, std::nullopt});
        require(!result.ok() && result.error == manage::data::QuoteErrorCode::Validation,
                "reverse date range must be rejected");
        result = repository.query({QDate(2026, 1, 1), QDate(2026, 1, 31), 0, std::nullopt});
        require(!result.ok() && result.error == manage::data::QuoteErrorCode::Validation,
                "non-positive customer id must be rejected");
        result = repository.query({QDate(2026, 1, 1), QDate(2026, 1, 31), std::nullopt, std::nullopt});
        require(!result.ok() && result.error == manage::data::QuoteErrorCode::Infrastructure,
                "closed database must produce infrastructure error");

        // 工程师责任制查询：参数校验必须先于数据库访问。
        manage::data::EngineerResponsibilityFilter engineerFilter{
            std::nullopt,
            manage::data::EngineerPeriodType::Month,
            QStringLiteral("2026-09"),
            QDate(2026, 9, 1),
            QDate(2026, 9, 30),
        };
        auto engineer = repository.queryEngineerResponsibility(engineerFilter);
        require(!engineer.ok() && engineer.error == manage::data::QuoteErrorCode::Infrastructure,
                "closed database must produce infrastructure error for engineer responsibility");
        engineer = repository.queryEngineerResponsibility({
            std::optional<qint64>{0},
            manage::data::EngineerPeriodType::Month,
            QStringLiteral("2026-09"),
            QDate(2026, 9, 1),
            QDate(2026, 9, 30),
        });
        require(!engineer.ok() && engineer.error == manage::data::QuoteErrorCode::Validation,
                "non-positive engineer id must be rejected");
        engineer = repository.queryEngineerResponsibility({
            std::nullopt,
            manage::data::EngineerPeriodType::Month,
            QStringLiteral("2026-09"),
            QDate(2026, 9, 30),
            QDate(2026, 9, 1),
        });
        require(!engineer.ok() && engineer.error == manage::data::QuoteErrorCode::Validation,
                "reverse engineer period must be rejected");
        engineer = repository.queryEngineerResponsibility({
            std::nullopt,
            manage::data::EngineerPeriodType::Month,
            QStringLiteral("2026-09"),
            QDate(),
            QDate(2026, 9, 30),
        });
        require(!engineer.ok() && engineer.error == manage::data::QuoteErrorCode::Validation,
                "invalid engineer period start must be rejected");
        engineer = repository.queryEngineerResponsibility({
            std::nullopt,
            manage::data::EngineerPeriodType::Year,
            QStringLiteral("9999"),
            QDate(9999, 1, 1),
            QDate(9999, 12, 31),
        });
        require(!engineer.ok() && engineer.error == manage::data::QuoteErrorCode::Validation,
                "engineer period end must allow an exclusive next day");
        std::cout << "[PASS] statistics repository validation and safe failure\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] statistics repository: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
