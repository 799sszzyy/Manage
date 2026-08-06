#pragma once

#include "manage/data/quote_models.h"

#include <QDate>
#include <QString>

#include <optional>
#include <vector>

namespace manage::data {

struct StatisticsFilter final {
    QDate startDate;
    QDate endDate;
    std::optional<qint64> customerId;
    std::optional<QuoteStatus> status;
};

struct StatisticsSummary final {
    qint64 quoteCount{};
    qint64 totalAmountCents{};
    qint64 averageAmountCents{};
    qint64 issuedCount{};
    qint64 voidCount{};
    // (issued + void) / all quotes, expressed as basis points. A void quote
    // was previously published. This is deliberately not a sales success rate.
    int publishedRateBasisPoints{};
};

struct StatisticsDimensionRow final {
    QString key;
    QString label;
    std::optional<qint64> entityId;
    qint64 quoteCount{};
    qint64 totalAmountCents{};
};

struct StatisticsReport final {
    StatisticsFilter filter;
    StatisticsSummary summary;
    std::vector<StatisticsDimensionRow> byMonth;
    std::vector<StatisticsDimensionRow> byCustomer;
    std::vector<StatisticsDimensionRow> byMaterialCategory;
};

// ---- 工程师责任制 ----

// 期间粒度：月 / 季度 / 年。
enum class EngineerPeriodType {
    Month,
    Quarter,
    Year,
};

// 检索条件：可选工程师账号 + 期间粒度 + 期间（如 2026-09 / 2026-Q3 / 2026）。
// 任务归属期间以销售预测的完成时间（expected_completion_at）为准。
struct EngineerResponsibilityFilter final {
    std::optional<qint64> engineerId;
    EngineerPeriodType periodType{EngineerPeriodType::Month};
    QString period;
    QDate periodStart;
    // 开区间右端：periodEnd 当天的 00:00 之前的时刻属于本期间。
    QDate periodEnd;
};

// 单个任务的完成情况：销售预测时间 vs 工程师实际提交时间。
struct EngineerTaskRow final {
    qint64 quoteId{};
    QString quoteNumber;
    QString customerName;
    QString engineerName;
    QuoteStatus status{QuoteStatus::Draft};
    QDateTime expectedCompletionAt;
    std::optional<QDateTime> submittedAt;
    bool onTime{};
    // 偏差天数 = 提交时间 - 预测时间；提前为负、逾期为正。
    qint64 deviationDays{};
};

// 一段期间内的完成情况汇总。
struct EngineerPeriodSummary final {
    qint64 assignedCount{};
    qint64 submittedCount{};
    qint64 unsubmittedCount{};
    qint64 onTimeCount{};
    qint64 lateCount{};
    // 准时率 = 准时数 / 已提交数，以基点表示（10000 = 100%）。
    int onTimeRateBasisPoints{};
    // 平均偏差天数：已提交任务的偏差均值（四舍五入）。
    qint64 averageDeviationDays{};
};

// 未指定工程师时，按工程师分组的汇总行。
struct EngineerSummaryRow final {
    qint64 engineerId{};
    QString engineerName;
    EngineerPeriodSummary summary;
};

struct EngineerResponsibilityReport final {
    EngineerResponsibilityFilter filter;
    // 指定工程师时的显示名；未指定时为空。
    QString engineerName;
    EngineerPeriodSummary summary;
    std::vector<EngineerSummaryRow> byEngineer;
    std::vector<EngineerTaskRow> tasks;
};

} // namespace manage::data
