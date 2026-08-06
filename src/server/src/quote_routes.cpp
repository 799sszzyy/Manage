#include "manage/server/quote_routes.h"

#include "manage/server/http_authorization.h"

#include "manage/auth/auth_service.h"
#include "manage/data/quote_lifecycle.h"

#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponder>
#include <QHttpServerResponse>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace manage::server {
namespace {

using StatusCode = QHttpServerResponder::StatusCode;
constexpr qint64 kMaxSafeJsonInteger = 9'007'199'254'740'991;

QHttpServerResponse errorResponse(
    const QString& code,
    const QString& message,
    StatusCode status
) {
    return QHttpServerResponse(
        QJsonObject{
            {QStringLiteral("error"), code},
            {QStringLiteral("message"), message},
        },
        status
    );
}

QHttpServerResponse lifecycleUnavailable() {
    return errorResponse(
        QStringLiteral("database_unavailable"),
        QStringLiteral("quote storage is unavailable"),
        StatusCode::ServiceUnavailable
    );
}

QHttpServerResponse lifecycleError(
    manage::data::QuoteErrorCode error,
    const QString& message
) {
    using ErrorCode = manage::data::QuoteErrorCode;
    switch (error) {
    case ErrorCode::Validation:
        return errorResponse(
            QStringLiteral("invalid_request"), message, StatusCode::BadRequest
        );
    case ErrorCode::NotFound:
        return errorResponse(
            QStringLiteral("not_found"), message, StatusCode::NotFound
        );
    case ErrorCode::Conflict:
        return errorResponse(
            QStringLiteral("revision_conflict"), message, StatusCode::Conflict
        );
    case ErrorCode::Duplicate:
        return errorResponse(
            QStringLiteral("duplicate_quote"), message, StatusCode::Conflict
        );
    case ErrorCode::InvalidTransition:
        return errorResponse(
            QStringLiteral("invalid_transition"), message, StatusCode::Conflict
        );
    case ErrorCode::Infrastructure:
        return errorResponse(
            QStringLiteral("database_error"),
            message,
            StatusCode::InternalServerError
        );
    case ErrorCode::None:
        break;
    }
    return errorResponse(
        QStringLiteral("server_error"),
        message.isEmpty() ? QStringLiteral("unable to process quote") : message,
        StatusCode::InternalServerError
    );
}

QJsonValue optionalIdJson(const std::optional<qint64>& id) {
    return id.has_value() ? QJsonValue(*id) : QJsonValue(QJsonValue::Null);
}

QJsonValue dateTimeJson(const QDateTime& value) {
    return value.isValid()
               ? QJsonValue(value.toUTC().toString(Qt::ISODateWithMs))
               : QJsonValue(QJsonValue::Null);
}

QJsonObject summaryJson(const manage::data::QuoteSummary& summary) {
    return {
        {QStringLiteral("id"), summary.id},
        {QStringLiteral("quoteNumber"), summary.quoteNumber},
        {QStringLiteral("customerId"), summary.customerId},
        {QStringLiteral("customerName"), summary.customerName},
        {QStringLiteral("bomTemplateId"), optionalIdJson(summary.bomTemplateId)},
        {QStringLiteral("bomQuantityMicros"), summary.bomQuantityMicros},
        {
            QStringLiteral("status"),
            manage::data::quoteStatusCode(summary.status)
        },
        {QStringLiteral("priceWithTaxCents"), summary.priceWithTaxCents},
        {QStringLiteral("revision"), summary.revision},
        {QStringLiteral("createdAt"), dateTimeJson(summary.createdAt)},
        {QStringLiteral("updatedAt"), dateTimeJson(summary.updatedAt)},
    };
}

QJsonObject documentJson(const manage::data::QuoteDocument& document) {
    auto object = summaryJson(document.summary);
    object.insert(QStringLiteral("customerContact"), document.customerContact);
    object.insert(QStringLiteral("customerPhone"), document.customerPhone);
    object.insert(QStringLiteral("customerAddress"), document.customerAddress);
    object.insert(
        QStringLiteral("materialCostCents"), document.materialCostCents
    );
    object.insert(QStringLiteral("freightCents"), document.freightCents);
    object.insert(QStringLiteral("otherFeesCents"), document.otherFeesCents);
    object.insert(
        QStringLiteral("markupBasisPoints"), document.markupBasisPoints
    );
    object.insert(
        QStringLiteral("markupAmountCents"), document.markupAmountCents
    );
    object.insert(
        QStringLiteral("priceBeforeTaxCents"), document.priceBeforeTaxCents
    );
    object.insert(QStringLiteral("taxBasisPoints"), document.taxBasisPoints);
    object.insert(QStringLiteral("taxAmountCents"), document.taxAmountCents);
    object.insert(
        QStringLiteral("priceWithTaxCents"), document.priceWithTaxCents
    );
    object.insert(QStringLiteral("notes"), document.notes);
    object.insert(
        QStringLiteral("sourceQuoteId"), optionalIdJson(document.sourceQuoteId)
    );
    object.insert(QStringLiteral("createdBy"), document.createdBy);
    object.insert(QStringLiteral("updatedBy"), document.updatedBy);
    object.insert(QStringLiteral("issuedAt"), dateTimeJson(document.issuedAt));
    object.insert(QStringLiteral("voidedAt"), dateTimeJson(document.voidedAt));

    QJsonArray items;
    for (const auto& item : document.items) {
        items.append(QJsonObject{
            {QStringLiteral("id"), item.id},
            {QStringLiteral("lineNo"), item.lineNo},
            {QStringLiteral("materialId"), item.materialId},
            {QStringLiteral("materialCode"), item.materialCode},
            {QStringLiteral("materialName"), item.materialName},
            {QStringLiteral("specification"), item.specification},
            {QStringLiteral("unit"), item.unit},
            {QStringLiteral("quantityMicros"), item.quantityMicros},
            {QStringLiteral("unitPriceCents"), item.unitPriceCents},
            {QStringLiteral("subtotalCents"), item.subtotalCents},
            {QStringLiteral("notes"), item.notes},
        });
    }
    object.insert(QStringLiteral("items"), items);
    return object;
}

QJsonObject pageJson(const manage::data::QuotePage& page) {
    QJsonArray items;
    for (const auto& item : page.items) {
        items.append(summaryJson(item));
    }
    return {
        {QStringLiteral("items"), items},
        {QStringLiteral("total"), page.total},
        {QStringLiteral("page"), page.page},
        {QStringLiteral("pageSize"), page.pageSize},
    };
}

std::optional<QJsonObject> requestObject(
    const QHttpServerRequest& request,
    QHttpServerResponse& failure
) {
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(request.body(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        failure = errorResponse(
            QStringLiteral("invalid_json"),
            QStringLiteral("request body must be a JSON object"),
            StatusCode::BadRequest
        );
        return std::nullopt;
    }
    return document.object();
}

bool readInteger(
    const QJsonObject& object,
    const QString& key,
    qint64& result,
    bool required,
    qint64 defaultValue = 0
) {
    if (!object.contains(key)) {
        if (required) {
            return false;
        }
        result = defaultValue;
        return true;
    }
    const auto value = object.value(key);
    if (!value.isDouble()) {
        return false;
    }
    const auto number = value.toDouble();
    if (!std::isfinite(number) || std::trunc(number) != number ||
        number < -static_cast<double>(kMaxSafeJsonInteger) ||
        number > static_cast<double>(kMaxSafeJsonInteger)) {
        return false;
    }
    result = static_cast<qint64>(number);
    return true;
}

bool readInt(
    const QJsonObject& object,
    const QString& key,
    int& result,
    bool required
) {
    qint64 number = 0;
    if (!readInteger(object, key, number, required) ||
        number < std::numeric_limits<int>::min() ||
        number > std::numeric_limits<int>::max()) {
        return false;
    }
    result = static_cast<int>(number);
    return true;
}

bool readString(
    const QJsonObject& object,
    const QString& key,
    QString& result,
    bool required
) {
    if (!object.contains(key)) {
        if (required) {
            return false;
        }
        result.clear();
        return true;
    }
    if (!object.value(key).isString()) {
        return false;
    }
    result = object.value(key).toString();
    return true;
}

bool readDraft(
    const QJsonObject& object,
    manage::data::QuoteDraft& draft,
    QString& message
) {
    if (!readInteger(object, QStringLiteral("customerId"), draft.customerId, true)) {
        message = QStringLiteral("customerId must be a safe integer");
        return false;
    }

    if (object.contains(QStringLiteral("bomTemplateId")) &&
        !object.value(QStringLiteral("bomTemplateId")).isNull()) {
        qint64 bomTemplateId = 0;
        if (!readInteger(
                object,
                QStringLiteral("bomTemplateId"),
                bomTemplateId,
                true
            )) {
            message = QStringLiteral("bomTemplateId must be null or a safe integer");
            return false;
        }
        draft.bomTemplateId = bomTemplateId;
    } else {
        draft.bomTemplateId.reset();
    }

    if (!readInteger(
            object,
            QStringLiteral("bomQuantityMicros"),
            draft.bomQuantityMicros,
            false,
            1'000'000
        )) {
        message = QStringLiteral("bomQuantityMicros must be a safe integer");
        return false;
    }

    if (!readInteger(
            object, QStringLiteral("freightCents"), draft.freightCents, true
        ) ||
        !readInteger(
            object, QStringLiteral("otherFeesCents"), draft.otherFeesCents, true
        ) ||
        !readInt(
            object,
            QStringLiteral("markupBasisPoints"),
            draft.markupBasisPoints,
            true
        ) ||
        !readInt(
            object,
            QStringLiteral("taxBasisPoints"),
            draft.taxBasisPoints,
            true
        )) {
        message = QStringLiteral(
            "freightCents, otherFeesCents, markupBasisPoints and "
            "taxBasisPoints must be safe integers"
        );
        return false;
    }
    if (!readString(object, QStringLiteral("notes"), draft.notes, false)) {
        message = QStringLiteral("notes must be a string");
        return false;
    }

    const auto itemsValue = object.value(QStringLiteral("items"));
    if (!itemsValue.isArray()) {
        message = QStringLiteral("items must be an array");
        return false;
    }
    const auto items = itemsValue.toArray();
    draft.items.clear();
    draft.items.reserve(static_cast<std::size_t>(items.size()));
    for (qsizetype index = 0; index < items.size(); ++index) {
        if (!items.at(index).isObject()) {
            message = QStringLiteral("items[%1] must be an object").arg(index);
            return false;
        }
        const auto item = items.at(index).toObject();
        manage::data::QuoteLineInput input;
        if (!readInteger(
                item, QStringLiteral("materialId"), input.materialId, true
            ) ||
            !readInteger(
                item,
                QStringLiteral("quantityMicros"),
                input.quantityMicros,
                true
            ) ||
            !readInteger(
                item,
                QStringLiteral("unitPriceCents"),
                input.unitPriceCents,
                true
            )) {
            message = QStringLiteral(
                "items[%1] materialId, quantityMicros and unitPriceCents "
                "must be safe integers"
            ).arg(index);
            return false;
        }
        if (!readString(item, QStringLiteral("notes"), input.notes, false)) {
            message = QStringLiteral("items[%1].notes must be a string").arg(index);
            return false;
        }
        draft.items.push_back(std::move(input));
    }
    return true;
}

std::optional<manage::data::QuoteSearchQuery> searchQuery(
    const QHttpServerRequest& request,
    QHttpServerResponse& failure
) {
    manage::data::QuoteSearchQuery result;
    const auto query = request.query();
    bool pageOk = true;
    bool pageSizeOk = true;
    if (query.hasQueryItem(QStringLiteral("page"))) {
        result.page = query.queryItemValue(QStringLiteral("page")).toInt(&pageOk);
    }
    if (query.hasQueryItem(QStringLiteral("pageSize"))) {
        result.pageSize = query.queryItemValue(QStringLiteral("pageSize"))
                              .toInt(&pageSizeOk);
    }
    if (!pageOk || !pageSizeOk || result.page < 1 || result.pageSize < 1 ||
        result.pageSize > 100) {
        failure = errorResponse(
            QStringLiteral("invalid_request"),
            QStringLiteral("page must be positive and pageSize must be from 1 to 100"),
            StatusCode::BadRequest
        );
        return std::nullopt;
    }
    result.search = query.queryItemValue(QStringLiteral("search"));
    if (query.hasQueryItem(QStringLiteral("status"))) {
        result.status = manage::data::quoteStatusFromCode(
            query.queryItemValue(QStringLiteral("status"))
        );
        if (!result.status.has_value()) {
            failure = errorResponse(
                QStringLiteral("invalid_request"),
                QStringLiteral("status must be draft, issued or void"),
                StatusCode::BadRequest
            );
            return std::nullopt;
        }
    }
    if (query.hasQueryItem(QStringLiteral("customerId"))) {
        bool idOk = true;
        const auto id = query.queryItemValue(QStringLiteral("customerId"))
                            .toLongLong(&idOk);
        if (!idOk || id <= 0) {
            failure = errorResponse(
                QStringLiteral("invalid_request"),
                QStringLiteral("customerId must be a positive integer"),
                StatusCode::BadRequest
            );
            return std::nullopt;
        }
        result.customerId = id;
    }
    return result;
}

struct AuthorizedActor final {
    qint64 id{};
    std::optional<QHttpServerResponse> failure;
};

AuthorizedActor authorize(
    const QHttpServerRequest& request,
    const std::shared_ptr<manage::auth::AuthService>& authService,
    std::initializer_list<manage::auth::UserRole> roles
) {
    if (!authService) {
        auto failure = HttpAuthorization::require(request, authService, roles);
        return {0, std::move(failure)};
    }
    const auto result = authService->authorize(
        HttpAuthorization::bearerToken(request), roles
    );
    if (!result.succeeded()) {
        return {0, HttpAuthorization::errorResponse(result)};
    }
    if (result.session.user.id >
        static_cast<quint64>(std::numeric_limits<qint64>::max())) {
        return {
            0,
            errorResponse(
                QStringLiteral("server_error"),
                QStringLiteral("authenticated user id is out of range"),
                StatusCode::InternalServerError
            ),
        };
    }
    return {static_cast<qint64>(result.session.user.id), std::nullopt};
}

QHttpServerResponse listQuotes(
    manage::data::QuoteLifecycle* lifecycle,
    const QHttpServerRequest& request
) {
    if (!lifecycle) {
        return lifecycleUnavailable();
    }
    QHttpServerResponse failure(StatusCode::BadRequest);
    auto query = searchQuery(request, failure);
    if (!query.has_value()) {
        return failure;
    }
    const auto result = lifecycle->list(std::move(*query));
    return result.ok() ? QHttpServerResponse(pageJson(*result.value))
                       : lifecycleError(result.error, result.message);
}

QHttpServerResponse getQuote(
    manage::data::QuoteLifecycle* lifecycle,
    qint64 id
) {
    if (!lifecycle) {
        return lifecycleUnavailable();
    }
    const auto result = lifecycle->getById(id);
    return result.ok() ? QHttpServerResponse(documentJson(*result.value))
                       : lifecycleError(result.error, result.message);
}

QHttpServerResponse createQuote(
    manage::data::QuoteLifecycle* lifecycle,
    const QHttpServerRequest& request,
    qint64 actorUserId
) {
    if (!lifecycle) {
        return lifecycleUnavailable();
    }
    QHttpServerResponse failure(StatusCode::BadRequest);
    const auto object = requestObject(request, failure);
    if (!object.has_value()) {
        return failure;
    }
    manage::data::CreateQuoteCommand command;
    QString message;
    if (!readDraft(*object, command.draft, message)) {
        return errorResponse(
            QStringLiteral("invalid_request"), message, StatusCode::BadRequest
        );
    }
    command.actorUserId = actorUserId;
    const auto result = lifecycle->create(std::move(command));
    return result.ok()
               ? QHttpServerResponse(
                     documentJson(*result.value), StatusCode::Created
                 )
               : lifecycleError(result.error, result.message);
}

QHttpServerResponse updateQuote(
    manage::data::QuoteLifecycle* lifecycle,
    qint64 id,
    const QHttpServerRequest& request,
    qint64 actorUserId
) {
    if (!lifecycle) {
        return lifecycleUnavailable();
    }
    QHttpServerResponse failure(StatusCode::BadRequest);
    const auto object = requestObject(request, failure);
    if (!object.has_value()) {
        return failure;
    }
    manage::data::UpdateQuoteCommand command;
    qint64 revision = 0;
    QString message;
    if (!readInteger(*object, QStringLiteral("revision"), revision, true) ||
        revision < std::numeric_limits<int>::min() ||
        revision > std::numeric_limits<int>::max()) {
        return errorResponse(
            QStringLiteral("invalid_request"),
            QStringLiteral("revision must be an integer"),
            StatusCode::BadRequest
        );
    }
    if (!readDraft(*object, command.draft, message)) {
        return errorResponse(
            QStringLiteral("invalid_request"), message, StatusCode::BadRequest
        );
    }
    command.id = id;
    command.expectedRevision = static_cast<int>(revision);
    command.actorUserId = actorUserId;
    const auto result = lifecycle->update(std::move(command));
    return result.ok() ? QHttpServerResponse(documentJson(*result.value))
                       : lifecycleError(result.error, result.message);
}

QHttpServerResponse changeQuoteStatus(
    manage::data::QuoteLifecycle* lifecycle,
    qint64 id,
    const QHttpServerRequest& request,
    qint64 actorUserId
) {
    if (!lifecycle) {
        return lifecycleUnavailable();
    }
    QHttpServerResponse failure(StatusCode::BadRequest);
    const auto object = requestObject(request, failure);
    if (!object.has_value()) {
        return failure;
    }
    qint64 revision = 0;
    QString status;
    if (!readInteger(*object, QStringLiteral("revision"), revision, true) ||
        revision < std::numeric_limits<int>::min() ||
        revision > std::numeric_limits<int>::max() ||
        !readString(*object, QStringLiteral("status"), status, true)) {
        return errorResponse(
            QStringLiteral("invalid_request"),
            QStringLiteral("status and integer revision are required"),
            StatusCode::BadRequest
        );
    }
    const auto target = manage::data::quoteStatusFromCode(status);
    if (!target.has_value() || *target == manage::data::QuoteStatus::Draft) {
        return errorResponse(
            QStringLiteral("invalid_request"),
            QStringLiteral("status must be issued or void"),
            StatusCode::BadRequest
        );
    }
    const auto result = lifecycle->changeStatus({
        id,
        static_cast<int>(revision),
        *target,
        actorUserId,
    });
    return result.ok() ? QHttpServerResponse(documentJson(*result.value))
                       : lifecycleError(result.error, result.message);
}

QHttpServerResponse cloneQuote(
    manage::data::QuoteLifecycle* lifecycle,
    qint64 id,
    qint64 actorUserId
) {
    if (!lifecycle) {
        return lifecycleUnavailable();
    }
    const auto result = lifecycle->clone({id, actorUserId});
    return result.ok()
               ? QHttpServerResponse(
                     documentJson(*result.value), StatusCode::Created
                 )
               : lifecycleError(result.error, result.message);
}

QHttpServerResponse deleteQuote(
    manage::data::QuoteLifecycle* lifecycle,
    qint64 id,
    const QHttpServerRequest& request,
    qint64 actorUserId
) {
    if (!lifecycle) {
        return lifecycleUnavailable();
    }
    const auto query = request.query();
    bool revisionOk = false;
    const auto revision = query.queryItemValue(QStringLiteral("revision"))
                              .toInt(&revisionOk);
    if (!query.hasQueryItem(QStringLiteral("revision")) || !revisionOk) {
        return errorResponse(
            QStringLiteral("invalid_request"),
            QStringLiteral("revision query parameter must be an integer"),
            StatusCode::BadRequest
        );
    }
    const auto result = lifecycle->deleteDraft({id, revision, actorUserId});
    return result.ok() ? QHttpServerResponse(StatusCode::NoContent)
                       : lifecycleError(result.error, result.message);
}

} // namespace

void QuoteRoutes::registerRoutes(
    QHttpServer& server,
    manage::data::QuoteLifecycle* lifecycle,
    const std::shared_ptr<manage::auth::AuthService>& authService
) {
    server.route(
        QStringLiteral("/api/v1/quotes"),
        QHttpServerRequest::Method::Get,
        [lifecycle, authService](const QHttpServerRequest& request) {
            auto actor = authorize(
                request,
                authService,
                {
                    manage::auth::UserRole::Admin,
                    manage::auth::UserRole::Quoter,
                    manage::auth::UserRole::Viewer,
                }
            );
            return actor.failure.has_value()
                       ? std::move(*actor.failure)
                       : listQuotes(lifecycle, request);
        }
    );
    server.route(
        QStringLiteral("/api/v1/quotes"),
        QHttpServerRequest::Method::Post,
        [lifecycle, authService](const QHttpServerRequest& request) {
            auto actor = authorize(
                request,
                authService,
                {
                    manage::auth::UserRole::Admin,
                    manage::auth::UserRole::Quoter,
                }
            );
            return actor.failure.has_value()
                       ? std::move(*actor.failure)
                       : createQuote(lifecycle, request, actor.id);
        }
    );
    server.route(
        QStringLiteral("/api/v1/quotes/<arg>/status"),
        QHttpServerRequest::Method::Patch,
        [lifecycle, authService](
            qint64 id,
            const QHttpServerRequest& request
        ) {
            auto actor = authorize(
                request,
                authService,
                {
                    manage::auth::UserRole::Admin,
                    manage::auth::UserRole::Quoter,
                }
            );
            return actor.failure.has_value()
                       ? std::move(*actor.failure)
                       : changeQuoteStatus(
                             lifecycle, id, request, actor.id
                         );
        }
    );
    server.route(
        QStringLiteral("/api/v1/quotes/<arg>/clone"),
        QHttpServerRequest::Method::Post,
        [lifecycle, authService](
            qint64 id,
            const QHttpServerRequest& request
        ) {
            auto actor = authorize(
                request,
                authService,
                {
                    manage::auth::UserRole::Admin,
                    manage::auth::UserRole::Quoter,
                }
            );
            return actor.failure.has_value()
                       ? std::move(*actor.failure)
                       : cloneQuote(lifecycle, id, actor.id);
        }
    );
    server.route(
        QStringLiteral("/api/v1/quotes/<arg>"),
        QHttpServerRequest::Method::Get,
        [lifecycle, authService](
            qint64 id,
            const QHttpServerRequest& request
        ) {
            auto actor = authorize(
                request,
                authService,
                {
                    manage::auth::UserRole::Admin,
                    manage::auth::UserRole::Quoter,
                    manage::auth::UserRole::Viewer,
                }
            );
            return actor.failure.has_value()
                       ? std::move(*actor.failure)
                       : getQuote(lifecycle, id);
        }
    );
    server.route(
        QStringLiteral("/api/v1/quotes/<arg>"),
        QHttpServerRequest::Method::Put,
        [lifecycle, authService](
            qint64 id,
            const QHttpServerRequest& request
        ) {
            auto actor = authorize(
                request,
                authService,
                {
                    manage::auth::UserRole::Admin,
                    manage::auth::UserRole::Quoter,
                }
            );
            return actor.failure.has_value()
                       ? std::move(*actor.failure)
                       : updateQuote(lifecycle, id, request, actor.id);
        }
    );
    server.route(
        QStringLiteral("/api/v1/quotes/<arg>"),
        QHttpServerRequest::Method::Delete,
        [lifecycle, authService](
            qint64 id,
            const QHttpServerRequest& request
        ) {
            auto actor = authorize(
                request,
                authService,
                {
                    manage::auth::UserRole::Admin,
                    manage::auth::UserRole::Quoter,
                }
            );
            return actor.failure.has_value()
                       ? std::move(*actor.failure)
                       : deleteQuote(lifecycle, id, request, actor.id);
        }
    );
}

} // namespace manage::server
