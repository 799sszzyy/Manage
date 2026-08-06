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

// ---- 工程师责任制 ----

QString engineerPeriodWhere(const EngineerResponsibilityFilter& filter) {
    QStringList conditions{
        QStringLiteral("q.assigned_engineer_id IS NOT NULL"),
        QStringLiteral("q.expected_completion_at >= :startAt"),
        QStringLiteral("q.expected_completion_at < :endAt"),
    };
    if (filter.engineerId.has_value()) {
        conditions.append(QStringLiteral("q.assigned_engineer_id = :engineerId"));
    }
    return conditions.join(QStringLiteral(" AND "));
}

void bindEngineerPeriod(QSqlQuery& query, const EngineerResponsibilityFilter& filter) {
    query.bindValue(QStringLiteral(":startAt"), QDateTime(filter.periodStart, QTime(0, 0)));
    query.bindValue(QStringLiteral(":endAt"), QDateTime(filter.periodEnd.addDays(1), QTime(0, 0)));
    if (filter.engineerId.has_value()) {
        query.bindValue(QStringLiteral(":engineerId"), idValue(*filter.engineerId));
    }
}

// 汇总列统一为：任务数、已提交、未提交、准时、逾期、平均偏差天数。
QString engineerSummarySelect() {
    return QStringLiteral(
        "COUNT(*), "
        "COALESCE(SUM(q.engineer_submitted_at IS NOT NULL), 0), "
        "COALESCE(SUM(q.engineer_submitted_at IS NULL), 0), "
        "COALESCE(SUM(q.engineer_submitted_at IS NOT NULL AND "
        "q.engineer_submitted_at <= q.expected_completion_at), 0), "
        "COALESCE(SUM(q.engineer_submitted_at IS NOT NULL AND "
        "q.engineer_submitted_at > q.expected_completion_at), 0), "
        "COALESCE(ROUND(AVG(CASE WHEN q.engineer_submitted_at IS NOT NULL "
        "THEN DATEDIFF(q.engineer_submitted_at, q.expected_completion_at) "
        "END)), 0)"
    );
}

EngineerPeriodSummary readEngineerSummary(const QSqlQuery& query, int columnOffset) {
    EngineerPeriodSummary summary;
    summary.assignedCount = integer(query.value(0 + columnOffset));
    summary.submittedCount = integer(query.value(1 + columnOffset));
    summary.unsubmittedCount = integer(query.value(2 + columnOffset));
    summary.onTimeCount = integer(query.value(3 + columnOffset));
    summary.lateCount = integer(query.value(4 + columnOffset));
    summary.averageDeviationDays = integer(query.value(5 + columnOffset));
    if (summary.submittedCount > 0) {
        summary.onTimeRateBasisPoints = static_cast<int>(
            (summary.onTimeCount * 10'000 + summary.submittedCount / 2) /
            summary.submittedCount
        );
    }
    return summary;
}

QuoteResult<EngineerResponsibilityReport>
MySqlStatisticsRepository::queryEngineerResponsibility(
    EngineerResponsibilityFilter filter
) {
    if (filter.engineerId.has_value() && *filter.engineerId <= 0) {
        return failure<EngineerResponsibilityReport>(
            QuoteErrorCode::Validation,
            QStringLiteral("engineerId must be greater than zero")
        );
    }
    if (!filter.periodStart.isValid() || !filter.periodEnd.isValid() ||
        filter.periodStart > filter.periodEnd ||
        !filter.periodEnd.addDays(1).isValid()) {
        return failure<EngineerResponsibilityReport>(
            QuoteErrorCode::Validation,
            QStringLiteral("engineer responsibility period is invalid")
        );
    }
    if (!database_.isValid() || !database_.isOpen()) {
        return failure<EngineerResponsibilityReport>(
            QuoteErrorCode::Infrastructure,
            QStringLiteral("statistics database is unavailable")
        );
    }

    ReadTransaction transaction(database_);
    if (!transaction.started()) {
        return failure<EngineerResponsibilityReport>(
            QuoteErrorCode::Infrastructure,
            QStringLiteral("unable to start engineer responsibility transaction: %1")
                .arg(database_.lastError().text())
        );
    }

    EngineerResponsibilityReport report;
    report.filter = filter;
    const auto where = engineerPeriodWhere(filter);

    if (filter.engineerId.has_value()) {
        QSqlQuery name(database_);
        name.prepare(QStringLiteral(
            "SELECT display_name FROM users WHERE id = :id"
        ));
        name.bindValue(QStringLiteral(":id"), idValue(*filter.engineerId));
        const auto nameExecuted = execute(
            name, QStringLiteral("unable to read engineer display name")
        );
        if (!nameExecuted.ok() || !name.next()) {
            return failure<EngineerResponsibilityReport>(
                QuoteErrorCode::Validation,
                QStringLiteral("engineer account does not exist")
            );
        }
        report.engineerName = name.value(0).toString();
    } else {
        QSqlQuery grouped(database_);
        grouped.prepare(QStringLiteral(
            "SELECT q.assigned_engineer_id, u.display_name, %1 FROM quotes q "
            "JOIN users u ON u.id = q.assigned_engineer_id "
            "WHERE %2 GROUP BY q.assigned_engineer_id, u.display_name "
            "ORDER BY COUNT(*) DESC, q.assigned_engineer_id"
        ).arg(engineerSummarySelect(), where));
        bindEngineerPeriod(grouped, filter);
        const auto groupedExecuted = execute(
            grouped, QStringLiteral("unable to aggregate engineer responsibility")
        );
        if (!groupedExecuted.ok()) {
            return failure<EngineerResponsibilityReport>(
                QuoteErrorCode::Infrastructure, groupedExecuted.message
            );
        }
        while (grouped.next()) {
            EngineerSummaryRow row;
            row.engineerId = integer(grouped.value(0));
            row.engineerName = grouped.value(1).toString();
            row.summary = readEngineerSummary(grouped, 2);
            report.byEngineer.push_back(std::move(row));
        }
    }

    QSqlQuery summary(database_);
    summary.prepare(QStringLiteral(
        "SELECT %1 FROM quotes q WHERE %2"
    ).arg(engineerSummarySelect(), where));
    bindEngineerPeriod(summary, filter);
    const auto summaryExecuted = execute(
        summary, QStringLiteral("unable to aggregate engineer responsibility summary")
    );
    if (!summaryExecuted.ok() || !summary.next()) {
        return failure<EngineerResponsibilityReport>(
            QuoteErrorCode::Infrastructure,
            summaryExecuted.ok()
                ? QStringLiteral("engineer responsibility summary row is missing")
                : summaryExecuted.message
        );
    }
    report.summary = readEngineerSummary(summary, 0);

    QSqlQuery tasks(database_);
    tasks.prepare(QStringLiteral(
        "SELECT q.id, q.quote_number, q.customer_name_snapshot, q.status, "
        "q.expected_completion_at, q.engineer_submitted_at, "
        "COALESCE(u.display_name, '') FROM quotes q "
        "LEFT JOIN users u ON u.id = q.assigned_engineer_id "
        "WHERE %1 ORDER BY q.expected_completion_at, q.id"
    ).arg(where));
    bindEngineerPeriod(tasks, filter);
    const auto tasksExecuted = execute(
        tasks, QStringLiteral("unable to list engineer responsibility tasks")
    );
    if (!tasksExecuted.ok()) {
        return failure<EngineerResponsibilityReport>(
            QuoteErrorCode::Infrastructure, tasksExecuted.message
        );
    }
    while (tasks.next()) {
        EngineerTaskRow row;
        row.quoteId = integer(tasks.value(0));
        row.quoteNumber = tasks.value(1).toString();
        row.customerName = tasks.value(2).toString();
        row.status = quoteStatusFromCode(tasks.value(3).toString())
                         .value_or(QuoteStatus::Draft);
        row.expectedCompletionAt = tasks.value(4).toDateTime();
        row.engineerName = tasks.value(6).toString();
        if (!tasks.value(5).isNull()) {
            row.submittedAt = tasks.value(5).toDateTime();
            row.onTime = *row.submittedAt <= row.expectedCompletionAt;
            // 偏差天数 = 提交 - 预测；提前为负、逾期为正。
            row.deviationDays =
                static_cast<qint64>(row.expectedCompletionAt.daysTo(*row.submittedAt));
        }
        report.tasks.push_back(std::move(row));
    }

    if (!transaction.commit()) {
        return failure<EngineerResponsibilityReport>(
            QuoteErrorCode::Infrastructure,
            QStringLiteral("unable to finish engineer responsibility transaction: %1")
                .arg(database_.lastError().text())
        );
    }
    return QuoteResult<EngineerResponsibilityReport>::success(std::move(report));
}

} // namespace manage::data
