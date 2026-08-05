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
        std::cout << "[PASS] statistics repository validation and safe failure\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] statistics repository: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
