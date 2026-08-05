#pragma once

#include "manage/data/statistics_repository.h"

#include <QSqlDatabase>

namespace manage::data {

class MySqlStatisticsRepository final : public StatisticsRepository {
public:
    explicit MySqlStatisticsRepository(QSqlDatabase database);
    QuoteResult<StatisticsReport> query(StatisticsFilter filter) override;

private:
    QSqlDatabase database_;
};

} // namespace manage::data
