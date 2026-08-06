#pragma once

#include "manage/data/statistics_models.h"

namespace manage::data {

class StatisticsRepository {
public:
    virtual ~StatisticsRepository() = default;
    virtual QuoteResult<StatisticsReport> query(StatisticsFilter filter) = 0;
    // 工程师责任制：按工程师 + 期间（月/季/年）统计任务完成情况与准时率。
    virtual QuoteResult<EngineerResponsibilityReport> queryEngineerResponsibility(
        EngineerResponsibilityFilter filter
    ) = 0;
};

} // namespace manage::data
