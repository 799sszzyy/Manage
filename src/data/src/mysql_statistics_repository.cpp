#include "manage/data/mysql_statistics_repository.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <utility>

namespace manage::data {
namespace {

template<typename T>
QuoteResult<T> failure(QuoteErrorCode code, const QString& message) {
    return QuoteResult<T>::failure(code, message);
}

QVariant idValue(qint64 value) {
    return QVariant::fromValue<qlonglong>(value);
}

class ReadTransaction final {
public:
    explicit ReadTransaction(QSqlDatabase database)
        : database_(std::move(database)), active_(database_.transaction()) {}
    ~ReadTransaction() { if (active_) database_.rollback(); }
    bool started() const noexcept { return active_; }
    bool commit() {
        if (!active_ || !database_.commit()) return false;
        active_ = false;
        return true;
    }
private:
    QSqlDatabase database_;
    bool active_{};
};

QString filterSql(const StatisticsFilter& filter) {
    QStringList conditions{
        QStringLiteral("q.created_at >= :startAt"),
        QStringLiteral("q.created_at < :endAt"),
    };
    if (filter.customerId.has_value()) conditions.append(QStringLiteral("q.customer_id = :customerId"));
    if (filter.status.has_value()) conditions.append(QStringLiteral("q.status = :status"));
    return conditions.join(QStringLiteral(" AND "));
}

void bindFilter(QSqlQuery& query, const StatisticsFilter& filter) {
    query.bindValue(QStringLiteral(":startAt"), QDateTime(filter.startDate, QTime(0, 0)));
    query.bindValue(QStringLiteral(":endAt"), QDateTime(filter.endDate.addDays(1), QTime(0, 0)));
    if (filter.customerId.has_value()) query.bindValue(QStringLiteral(":customerId"), idValue(*filter.customerId));
    if (filter.status.has_value()) query.bindValue(QStringLiteral(":status"), quoteStatusCode(*filter.status));
}

QuoteResult<bool> execute(QSqlQuery& query, const QString& operation) {
    if (!query.exec()) {
        return QuoteResult<bool>::failure(
            QuoteErrorCode::Infrastructure,
            QStringLiteral("%1: %2").arg(operation, query.lastError().text())
        );
    }
    return QuoteResult<bool>::success(true);
}

qint64 integer(const QVariant& value) {
    return value.isNull() ? 0 : value.toLongLong();
}

QuoteResult<std::vector<StatisticsDimensionRow>> groupedRows(
    QSqlDatabase database,
    const StatisticsFilter& filter,
    const QString& sql,
    bool hasEntityId,
    const QString& operation
) {
    QSqlQuery query(database);
    query.prepare(sql);
    bindFilter(query, filter);
    const auto executed = execute(query, operation);
    if (!executed.ok()) {
        return QuoteResult<std::vector<StatisticsDimensionRow>>::failure(
            executed.error, executed.message
        );
    }
    std::vector<StatisticsDimensionRow> rows;
    while (query.next()) {
        StatisticsDimensionRow row;
        row.key = query.value(0).toString();
        row.label = query.value(1).toString();
        const auto offset = hasEntityId ? 1 : 0;
        if (hasEntityId) row.entityId = integer(query.value(2));
        row.quoteCount = integer(query.value(2 + offset));
        row.totalAmountCents = integer(query.value(3 + offset));
        rows.push_back(std::move(row));
    }
    return QuoteResult<std::vector<StatisticsDimensionRow>>::success(std::move(rows));
}

} // namespace

MySqlStatisticsRepository::MySqlStatisticsRepository(QSqlDatabase database)
    : database_(std::move(database)) {}

QuoteResult<StatisticsReport> MySqlStatisticsRepository::query(StatisticsFilter filter) {
    if (!filter.startDate.isValid() || !filter.endDate.isValid()) {
        return failure<StatisticsReport>(QuoteErrorCode::Validation, QStringLiteral("startDate and endDate must be valid dates"));
    }
    if (filter.startDate > filter.endDate || !filter.endDate.addDays(1).isValid()) {
        return failure<StatisticsReport>(QuoteErrorCode::Validation, QStringLiteral("startDate must not be after endDate"));
    }
    if (filter.customerId.has_value() && *filter.customerId <= 0) {
        return failure<StatisticsReport>(QuoteErrorCode::Validation, QStringLiteral("customerId must be greater than zero"));
    }
    if (!database_.isValid() || !database_.isOpen()) {
        return failure<StatisticsReport>(QuoteErrorCode::Infrastructure, QStringLiteral("statistics database is unavailable"));
    }

    ReadTransaction transaction(database_);
    if (!transaction.started()) {
        return failure<StatisticsReport>(QuoteErrorCode::Infrastructure, QStringLiteral("unable to start statistics transaction: %1").arg(database_.lastError().text()));
    }

    StatisticsReport report;
    report.filter = filter;
    const auto where = filterSql(filter);

    QSqlQuery summary(database_);
    summary.prepare(QStringLiteral(
        "SELECT COUNT(*), COALESCE(SUM(q.price_with_tax_cents), 0), "
        "COALESCE(ROUND(AVG(q.price_with_tax_cents)), 0), "
        "COALESCE(SUM(q.status = 'issued'), 0), "
        "COALESCE(SUM(q.status = 'void'), 0) FROM quotes q WHERE %1"
    ).arg(where));
    bindFilter(summary, filter);
    const auto summaryExecuted = execute(summary, QStringLiteral("unable to aggregate statistics summary"));
    if (!summaryExecuted.ok() || !summary.next()) {
        return failure<StatisticsReport>(QuoteErrorCode::Infrastructure,
            summaryExecuted.ok() ? QStringLiteral("statistics summary row is missing") : summaryExecuted.message);
    }
    report.summary.quoteCount = integer(summary.value(0));
    report.summary.totalAmountCents = integer(summary.value(1));
    report.summary.averageAmountCents = integer(summary.value(2));
    report.summary.issuedCount = integer(summary.value(3));
    report.summary.voidCount = integer(summary.value(4));
    if (report.summary.quoteCount > 0) {
        const auto published = report.summary.issuedCount + report.summary.voidCount;
        report.summary.publishedRateBasisPoints = static_cast<int>(
            (published * 10'000 + report.summary.quoteCount / 2) /
            report.summary.quoteCount
        );
    }

    auto months = groupedRows(
        database_, filter,
        QStringLiteral(
            "SELECT DATE_FORMAT(q.created_at, '%Y-%m'), DATE_FORMAT(q.created_at, '%Y-%m'), "
            "COUNT(*), COALESCE(SUM(q.price_with_tax_cents), 0) FROM quotes q "
            "WHERE %1 GROUP BY DATE_FORMAT(q.created_at, '%Y-%m') ORDER BY 1"
        ).arg(where), false, QStringLiteral("unable to aggregate statistics by month")
    );
    if (!months.ok()) return failure<StatisticsReport>(months.error, months.message);
    report.byMonth = std::move(*months.value);

    auto customers = groupedRows(
        database_, filter,
        QStringLiteral(
            "SELECT CAST(q.customer_id AS CHAR), MAX(q.customer_name_snapshot), q.customer_id, "
            "COUNT(*), COALESCE(SUM(q.price_with_tax_cents), 0) FROM quotes q "
            "WHERE %1 GROUP BY q.customer_id "
            "ORDER BY SUM(q.price_with_tax_cents) DESC, q.customer_id"
        ).arg(where), true, QStringLiteral("unable to aggregate statistics by customer")
    );
    if (!customers.ok()) return failure<StatisticsReport>(customers.error, customers.message);
    report.byCustomer = std::move(*customers.value);

    auto categories = groupedRows(
        database_, filter,
        QStringLiteral(
            "SELECT COALESCE(NULLIF(m.category, ''), '未分类'), "
            "COALESCE(NULLIF(m.category, ''), '未分类'), COUNT(DISTINCT q.id), "
            "COALESCE(SUM(qi.subtotal_cents), 0) FROM quotes q "
            "JOIN quote_items qi ON qi.quote_id = q.id "
            "JOIN materials m ON m.id = qi.material_id WHERE %1 "
            "GROUP BY COALESCE(NULLIF(m.category, ''), '未分类') "
            "ORDER BY SUM(qi.subtotal_cents) DESC, 1"
        ).arg(where), false, QStringLiteral("unable to aggregate statistics by material category")
    );
    if (!categories.ok()) return failure<StatisticsReport>(categories.error, categories.message);
    report.byMaterialCategory = std::move(*categories.value);

    if (!transaction.commit()) {
        return failure<StatisticsReport>(QuoteErrorCode::Infrastructure, QStringLiteral("unable to finish statistics transaction: %1").arg(database_.lastError().text()));
    }
    return QuoteResult<StatisticsReport>::success(std::move(report));
}

} // namespace manage::data
