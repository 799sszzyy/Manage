#include "manage/server/statistics_routes.h"

#include "manage/auth/auth_service.h"
#include "manage/data/statistics_repository.h"
#include "manage/server/http_authorization.h"

#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponder>
#include <QHttpServerResponse>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrlQuery>

#include <optional>
#include <utility>

namespace manage::server {
namespace {

using StatusCode = QHttpServerResponder::StatusCode;

QHttpServerResponse errorResponse(const QString& code, const QString& message, StatusCode status) {
    return QHttpServerResponse(QJsonObject{
        {QStringLiteral("error"), code},
        {QStringLiteral("message"), message},
    }, status);
}

std::optional<QDate> dateParameter(const QUrlQuery& query, const QString& name) {
    const auto text = query.queryItemValue(name, QUrl::FullyDecoded);
    const auto result = QDate::fromString(text, QStringLiteral("yyyy-MM-dd"));
    if (!result.isValid() || result.toString(QStringLiteral("yyyy-MM-dd")) != text) return std::nullopt;
    return result;
}

QJsonArray dimensionJson(const std::vector<manage::data::StatisticsDimensionRow>& rows) {
    QJsonArray result;
    for (const auto& row : rows) {
        QJsonObject object{
            {QStringLiteral("key"), row.key},
            {QStringLiteral("label"), row.label},
            {QStringLiteral("quoteCount"), row.quoteCount},
            {QStringLiteral("totalAmountCents"), row.totalAmountCents},
        };
        object.insert(
            QStringLiteral("entityId"),
            row.entityId.has_value() ? QJsonValue(*row.entityId) : QJsonValue(QJsonValue::Null)
        );
        result.append(object);
    }
    return result;
}

QHttpServerResponse statisticsResponse(
    manage::data::StatisticsRepository* repository,
    const QHttpServerRequest& request
) {
    if (!repository) {
        return errorResponse(QStringLiteral("database_unavailable"), QStringLiteral("statistics storage is unavailable"), StatusCode::ServiceUnavailable);
    }
    const QUrlQuery query(request.url());
    const auto startDate = dateParameter(query, QStringLiteral("startDate"));
    const auto endDate = dateParameter(query, QStringLiteral("endDate"));
    if (!startDate.has_value() || !endDate.has_value()) {
        return errorResponse(QStringLiteral("invalid_request"), QStringLiteral("startDate and endDate must use YYYY-MM-DD"), StatusCode::BadRequest);
    }

    manage::data::StatisticsFilter filter{*startDate, *endDate, std::nullopt, std::nullopt};
    const auto customerText = query.queryItemValue(QStringLiteral("customerId"), QUrl::FullyDecoded).trimmed();
    if (!customerText.isEmpty()) {
        bool ok = false;
        const auto customerId = customerText.toLongLong(&ok);
        if (!ok || customerId <= 0) {
            return errorResponse(QStringLiteral("invalid_request"), QStringLiteral("customerId must be a positive integer"), StatusCode::BadRequest);
        }
        filter.customerId = customerId;
    }
    const auto statusText = query.queryItemValue(QStringLiteral("status"), QUrl::FullyDecoded).trimmed();
    if (!statusText.isEmpty()) {
        filter.status = manage::data::quoteStatusFromCode(statusText);
        if (!filter.status.has_value()) {
            return errorResponse(QStringLiteral("invalid_request"), QStringLiteral("status must be draft, issued or void"), StatusCode::BadRequest);
        }
    }

    auto report = repository->query(filter);
    if (!report.ok()) {
        const auto validation = report.error == manage::data::QuoteErrorCode::Validation;
        return errorResponse(
            validation ? QStringLiteral("invalid_request") : QStringLiteral("database_error"),
            report.message,
            validation ? StatusCode::BadRequest : StatusCode::InternalServerError
        );
    }
    const auto& value = *report.value;
    return QHttpServerResponse(QJsonObject{
        {QStringLiteral("filters"), QJsonObject{
            {QStringLiteral("startDate"), value.filter.startDate.toString(QStringLiteral("yyyy-MM-dd"))},
            {QStringLiteral("endDate"), value.filter.endDate.toString(QStringLiteral("yyyy-MM-dd"))},
            {QStringLiteral("customerId"), value.filter.customerId.has_value() ? QJsonValue(*value.filter.customerId) : QJsonValue(QJsonValue::Null)},
            {QStringLiteral("status"), value.filter.status.has_value() ? QJsonValue(manage::data::quoteStatusCode(*value.filter.status)) : QJsonValue(QJsonValue::Null)},
        }},
        {QStringLiteral("summary"), QJsonObject{
            {QStringLiteral("quoteCount"), value.summary.quoteCount},
            {QStringLiteral("totalAmountCents"), value.summary.totalAmountCents},
            {QStringLiteral("averageAmountCents"), value.summary.averageAmountCents},
            {QStringLiteral("issuedCount"), value.summary.issuedCount},
            {QStringLiteral("voidCount"), value.summary.voidCount},
            {QStringLiteral("publishedRateBasisPoints"), value.summary.publishedRateBasisPoints},
        }},
        {QStringLiteral("byMonth"), dimensionJson(value.byMonth)},
        {QStringLiteral("byCustomer"), dimensionJson(value.byCustomer)},
        {QStringLiteral("byMaterialCategory"), dimensionJson(value.byMaterialCategory)},
    });
}

// ---- 工程师责任制 ----

std::optional<std::pair<QDate, QDate>> engineerPeriodRange(
    const QString& periodType,
    const QString& period
) {
    if (periodType == QStringLiteral("month")) {
        static const QRegularExpression pattern(
            QStringLiteral(R"(^(\d{4})-(0[1-9]|1[0-2])$)")
        );
        const auto match = pattern.match(period);
        if (!match.hasMatch()) return std::nullopt;
        const auto year = match.captured(1).toInt();
        const auto month = match.captured(2).toInt();
        const auto start = QDate(year, month, 1);
        return std::pair{start, QDate(year, month, start.daysInMonth())};
    }
    if (periodType == QStringLiteral("quarter")) {
        static const QRegularExpression pattern(
            QStringLiteral(R"(^(\d{4})-Q([1-4])$)")
        );
        const auto match = pattern.match(period);
        if (!match.hasMatch()) return std::nullopt;
        const auto year = match.captured(1).toInt();
        const auto quarter = match.captured(2).toInt();
        const auto startMonth = quarter * 3 - 2;
        const auto start = QDate(year, startMonth, 1);
        // 结束月为 startMonth+2；用结束月自己的天数计算末日（如 9 月=30 天）。
        return std::pair{
            start,
            QDate(year, startMonth + 2, QDate(year, startMonth + 2, 1).daysInMonth()),
        };
    }
    if (periodType == QStringLiteral("year")) {
        static const QRegularExpression pattern(QStringLiteral(R"(^(\d{4})$)"));
        const auto match = pattern.match(period);
        if (!match.hasMatch()) return std::nullopt;
        const auto year = match.captured(1).toInt();
        return std::pair{QDate(year, 1, 1), QDate(year, 12, 31)};
    }
    return std::nullopt;
}

QJsonObject engineerSummaryJson(const manage::data::EngineerPeriodSummary& summary) {
    return QJsonObject{
        {QStringLiteral("assignedCount"), summary.assignedCount},
        {QStringLiteral("submittedCount"), summary.submittedCount},
        {QStringLiteral("unsubmittedCount"), summary.unsubmittedCount},
        {QStringLiteral("onTimeCount"), summary.onTimeCount},
        {QStringLiteral("lateCount"), summary.lateCount},
        {QStringLiteral("onTimeRateBasisPoints"), summary.onTimeRateBasisPoints},
        {QStringLiteral("averageDeviationDays"), summary.averageDeviationDays},
    };
}

QJsonArray engineerTaskJson(const std::vector<manage::data::EngineerTaskRow>& tasks) {
    QJsonArray result;
    for (const auto& task : tasks) {
        QJsonObject object{
            {QStringLiteral("quoteId"), task.quoteId},
            {QStringLiteral("quoteNumber"), task.quoteNumber},
            {QStringLiteral("customerName"), task.customerName},
            {QStringLiteral("engineerName"), task.engineerName},
            {QStringLiteral("status"), manage::data::quoteStatusCode(task.status)},
            {QStringLiteral("onTime"), task.onTime},
            {QStringLiteral("deviationDays"), task.deviationDays},
            {QStringLiteral("expectedCompletionAt"), task.expectedCompletionAt.isValid()
                 ? QJsonValue(task.expectedCompletionAt.toUTC().toString(Qt::ISODateWithMs))
                 : QJsonValue(QJsonValue::Null)},
        };
        object.insert(
            QStringLiteral("submittedAt"),
            task.submittedAt.has_value()
                ? QJsonValue(task.submittedAt->toUTC().toString(Qt::ISODateWithMs))
                : QJsonValue(QJsonValue::Null)
        );
        result.append(object);
    }
    return result;
}

QHttpServerResponse engineerResponsibilityResponse(
    manage::data::StatisticsRepository* repository,
    const QHttpServerRequest& request
) {
    if (!repository) {
        return errorResponse(QStringLiteral("database_unavailable"), QStringLiteral("statistics storage is unavailable"), StatusCode::ServiceUnavailable);
    }
    const QUrlQuery query(request.url());
    const auto periodType = query.queryItemValue(
        QStringLiteral("periodType"), QUrl::FullyDecoded
    ).trimmed();
    const auto period = query.queryItemValue(
        QStringLiteral("period"), QUrl::FullyDecoded
    ).trimmed();
    const auto range = engineerPeriodRange(periodType, period);
    if (!range.has_value()) {
        return errorResponse(
            QStringLiteral("invalid_request"),
            QStringLiteral("periodType must be month, quarter or year, and period must match it (e.g. 2026-09 / 2026-Q3 / 2026)"),
            StatusCode::BadRequest
        );
    }

    manage::data::EngineerResponsibilityFilter filter;
    filter.periodType = periodType == QStringLiteral("quarter")
                            ? manage::data::EngineerPeriodType::Quarter
                        : periodType == QStringLiteral("year")
                            ? manage::data::EngineerPeriodType::Year
                            : manage::data::EngineerPeriodType::Month;
    filter.period = period;
    filter.periodStart = range->first;
    filter.periodEnd = range->second;
    const auto engineerText = query.queryItemValue(
        QStringLiteral("engineerId"), QUrl::FullyDecoded
    ).trimmed();
    if (!engineerText.isEmpty()) {
        bool ok = false;
        const auto engineerId = engineerText.toLongLong(&ok);
        if (!ok || engineerId <= 0) {
            return errorResponse(QStringLiteral("invalid_request"), QStringLiteral("engineerId must be a positive integer"), StatusCode::BadRequest);
        }
        filter.engineerId = engineerId;
    }

    auto report = repository->queryEngineerResponsibility(filter);
    if (!report.ok()) {
        const auto validation = report.error == manage::data::QuoteErrorCode::Validation;
        return errorResponse(
            validation ? QStringLiteral("invalid_request") : QStringLiteral("database_error"),
            report.message,
            validation ? StatusCode::BadRequest : StatusCode::InternalServerError
        );
    }
    const auto& value = *report.value;
    QJsonArray byEngineer;
    for (const auto& row : value.byEngineer) {
        byEngineer.append(QJsonObject{
            {QStringLiteral("engineerId"), row.engineerId},
            {QStringLiteral("engineerName"), row.engineerName},
            {QStringLiteral("summary"), engineerSummaryJson(row.summary)},
        });
    }
    return QHttpServerResponse(QJsonObject{
        {QStringLiteral("filters"), QJsonObject{
            {QStringLiteral("engineerId"), value.filter.engineerId.has_value()
                 ? QJsonValue(*value.filter.engineerId)
                 : QJsonValue(QJsonValue::Null)},
            {QStringLiteral("periodType"), periodType},
            {QStringLiteral("period"), value.filter.period},
            {QStringLiteral("periodStart"), value.filter.periodStart.toString(QStringLiteral("yyyy-MM-dd"))},
            {QStringLiteral("periodEnd"), value.filter.periodEnd.toString(QStringLiteral("yyyy-MM-dd"))},
        }},
        {QStringLiteral("engineerName"), value.engineerName},
        {QStringLiteral("summary"), engineerSummaryJson(value.summary)},
        {QStringLiteral("byEngineer"), byEngineer},
        {QStringLiteral("tasks"), engineerTaskJson(value.tasks)},
    });
}

} // namespace

void StatisticsRoutes::registerRoutes(
    QHttpServer& server,
    manage::data::StatisticsRepository* repository,
    const std::shared_ptr<manage::auth::AuthService>& authService
) {
    server.route(
        QStringLiteral("/api/v1/statistics"),
        QHttpServerRequest::Method::Get,
        [repository, authService](const QHttpServerRequest& request) {
            auto failure = HttpAuthorization::require(
                request,
                authService,
                {manage::auth::UserRole::Admin, manage::auth::UserRole::Quoter, manage::auth::UserRole::Viewer}
            );
            return failure.has_value() ? std::move(*failure) : statisticsResponse(repository, request);
        }
    );
    server.route(
        QStringLiteral("/api/v1/statistics/engineer-responsibility"),
        QHttpServerRequest::Method::Get,
        [repository, authService](const QHttpServerRequest& request) {
            auto failure = HttpAuthorization::require(
                request,
                authService,
                {manage::auth::UserRole::Admin, manage::auth::UserRole::Quoter, manage::auth::UserRole::Viewer}
            );
            return failure.has_value()
                       ? std::move(*failure)
                       : engineerResponsibilityResponse(repository, request);
        }
    );
}

} // namespace manage::server
