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

} // namespace manage::data
