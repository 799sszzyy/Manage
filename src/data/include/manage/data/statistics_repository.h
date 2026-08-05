#pragma once

#include "manage/data/statistics_models.h"

namespace manage::data {

class StatisticsRepository {
public:
    virtual ~StatisticsRepository() = default;
    virtual QuoteResult<StatisticsReport> query(StatisticsFilter filter) = 0;
};

} // namespace manage::data
