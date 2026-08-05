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
#include <QUrlQuery>

#include <optional>

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
}

} // namespace manage::server
