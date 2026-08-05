#include "manage/server/api_server.h"
#include "manage/server/bom_routes.h"

#include "manage/auth/auth_service.h"
#include "manage/server/catalog_routes.h"
#include "manage/server/http_authorization.h"
#include "manage/server/material_batch_routes.h"
#include "manage/server/quote_routes.h"
#include "manage/server/statistics_routes.h"
#include "manage/server/user_routes.h"

#include "manage/data/catalog_repository.h"
#include "manage/data/catalog_service.h"
#include "manage/data/database_connection.h"
#include "manage/domain/quote_calculator.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QHttpServerResponder>

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace manage::server {
namespace {

using StatusCode = QHttpServerResponder::StatusCode;

constexpr qint64 kMaxSafeJsonInteger = 9'007'199'254'740'991;

QJsonValue jsonInteger(std::int64_t value) {
    return QJsonValue(static_cast<qint64>(value));
}

QHttpServerResponse errorResponse(
    const QString& code,
    const QString& message,
    StatusCode status = StatusCode::BadRequest
) {
    return QHttpServerResponse(
        QJsonObject{
            {QStringLiteral("error"), code},
            {QStringLiteral("message"), message},
        },
        status
    );
}

QJsonObject userJson(const manage::auth::AuthenticatedUser& user) {
    return {
        {QStringLiteral("id"), jsonInteger(static_cast<qint64>(user.id))},
        {QStringLiteral("username"), user.username},
        {QStringLiteral("displayName"), user.displayName},
        {QStringLiteral("role"), manage::auth::roleCode(user.role)},
        {QStringLiteral("mustChangePassword"), user.mustChangePassword},
    };
}

bool parseObjectBody(
    const QHttpServerRequest& request,
    QJsonObject* object,
    QHttpServerResponse* failureResponse
) {
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(request.body(), &parseError);
    if (parseError.error == QJsonParseError::NoError && document.isObject()) {
        *object = document.object();
        return true;
    }
    *failureResponse = errorResponse(
        QStringLiteral("invalid_json"),
        QStringLiteral("request body must be a JSON object")
    );
    return false;
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

    const auto numeric = value.toDouble();
    if (!std::isfinite(numeric) || std::trunc(numeric) != numeric ||
        numeric < -static_cast<double>(kMaxSafeJsonInteger) ||
        numeric > static_cast<double>(kMaxSafeJsonInteger)) {
        return false;
    }

    result = static_cast<qint64>(numeric);
    return true;
}

QString domainErrorCode(manage::domain::QuoteCalculationErrorCode code) {
    using ErrorCode = manage::domain::QuoteCalculationErrorCode;
    switch (code) {
    case ErrorCode::NonPositiveQuantity:
        return QStringLiteral("non_positive_quantity");
    case ErrorCode::NegativeUnitPrice:
        return QStringLiteral("negative_unit_price");
    case ErrorCode::NegativeFreight:
        return QStringLiteral("negative_freight");
    case ErrorCode::NegativeOtherFees:
        return QStringLiteral("negative_other_fees");
    case ErrorCode::MarkupOutOfRange:
        return QStringLiteral("markup_out_of_range");
    case ErrorCode::TaxOutOfRange:
        return QStringLiteral("tax_out_of_range");
    case ErrorCode::AmountOverflow:
        return QStringLiteral("amount_overflow");
    }
    return QStringLiteral("calculation_error");
}

} // namespace

ApiServer::ApiServer()
    : ApiServer(
          nullptr,
          nullptr,
          nullptr,
          nullptr,
          std::shared_ptr<manage::auth::UserManagementService>{},
          nullptr,
          nullptr
      ) {}

ApiServer::ApiServer(std::shared_ptr<manage::auth::AuthService> authService)
    : ApiServer(
          std::move(authService),
          nullptr,
          nullptr,
          nullptr,
          std::shared_ptr<manage::auth::UserManagementService>{},
          nullptr,
          nullptr
      ) {}

ApiServer::ApiServer(
    std::shared_ptr<manage::data::CatalogRepository> catalogRepository
) : ApiServer(
        nullptr,
        std::move(catalogRepository),
        nullptr,
        nullptr,
        std::shared_ptr<manage::auth::UserManagementService>{},
        nullptr,
        nullptr
    ) {}

ApiServer::ApiServer(
    std::shared_ptr<manage::auth::AuthService> authService,
    std::shared_ptr<manage::data::CatalogRepository> catalogRepository
) : ApiServer(
        std::move(authService),
        std::move(catalogRepository),
        nullptr,
        nullptr,
        std::shared_ptr<manage::auth::UserManagementService>{},
        nullptr,
        nullptr
    ) {}

ApiServer::ApiServer(manage::data::BomService* bomService)
    : ApiServer(
          nullptr,
          nullptr,
          bomService,
          nullptr,
          std::shared_ptr<manage::auth::UserManagementService>{},
          nullptr,
          nullptr
      ) {}

ApiServer::ApiServer(
    std::shared_ptr<manage::auth::AuthService> authService,
    std::shared_ptr<manage::data::CatalogRepository> catalogRepository,
    manage::data::BomService* bomService
) : ApiServer(
        std::move(authService),
        std::move(catalogRepository),
        bomService,
        nullptr,
        std::shared_ptr<manage::auth::UserManagementService>{},
        nullptr,
        nullptr
    ) {}

ApiServer::ApiServer(
    std::shared_ptr<manage::auth::AuthService> authService,
    std::shared_ptr<manage::data::CatalogRepository> catalogRepository,
    manage::data::BomService* bomService,
    manage::data::QuoteLifecycle* quoteLifecycle
) : ApiServer(
        std::move(authService),
        std::move(catalogRepository),
        bomService,
        quoteLifecycle,
        std::shared_ptr<manage::auth::UserManagementService>{},
        nullptr,
        nullptr
    ) {}

ApiServer::ApiServer(
    std::shared_ptr<manage::auth::AuthService> authService,
    std::shared_ptr<manage::data::CatalogRepository> catalogRepository,
    manage::data::BomService* bomService,
    manage::data::QuoteLifecycle* quoteLifecycle,
    std::shared_ptr<manage::auth::UserManagementService> userManagementService
) : ApiServer(
        std::move(authService),
        std::move(catalogRepository),
        bomService,
        quoteLifecycle,
        std::move(userManagementService),
        nullptr,
        nullptr
    ) {}

ApiServer::ApiServer(
    std::shared_ptr<manage::auth::AuthService> authService,
    std::shared_ptr<manage::data::CatalogRepository> catalogRepository,
    manage::data::BomService* bomService,
    manage::data::QuoteLifecycle* quoteLifecycle,
    manage::data::StatisticsRepository* statisticsRepository
) : ApiServer(
        std::move(authService),
        std::move(catalogRepository),
        bomService,
        quoteLifecycle,
        std::shared_ptr<manage::auth::UserManagementService>{},
        statisticsRepository,
        nullptr
    ) {}

ApiServer::ApiServer(
    std::shared_ptr<manage::auth::AuthService> authService,
    std::shared_ptr<manage::data::CatalogRepository> catalogRepository,
    manage::data::BomService* bomService,
    manage::data::QuoteLifecycle* quoteLifecycle,
    std::shared_ptr<manage::auth::UserManagementService> userManagementService,
    manage::data::StatisticsRepository* statisticsRepository,
    manage::data::MaterialBatchService* materialBatchService
) : tcpServer_(new QTcpServer(&server_)),
    authService_(std::move(authService)) {
    server_.route(
        QStringLiteral("/api/v1/health"),
        QHttpServerRequest::Method::Get,
        [this]() { return healthResponse(); }
    );

    server_.route(
        QStringLiteral("/api/v1/quotes/calculate"),
        QHttpServerRequest::Method::Post,
        [this](const QHttpServerRequest& request) {
            return calculateQuoteResponse(request);
        }
    );

    server_.route(
        QStringLiteral("/api/v1/auth/bootstrap"),
        QHttpServerRequest::Method::Post,
        [this](const QHttpServerRequest& request) {
            return bootstrapResponse(request);
        }
    );

    server_.route(
        QStringLiteral("/api/v1/auth/login"),
        QHttpServerRequest::Method::Post,
        [this](const QHttpServerRequest& request) {
            return loginResponse(request);
        }
    );

    server_.route(
        QStringLiteral("/api/v1/auth/logout"),
        QHttpServerRequest::Method::Post,
        [this](const QHttpServerRequest& request) {
            return logoutResponse(request);
        }
    );

    server_.route(
        QStringLiteral("/api/v1/auth/me"),
        QHttpServerRequest::Method::Get,
        [this](const QHttpServerRequest& request) {
            return meResponse(request);
        }
    );

    server_.route(
        QStringLiteral("/api/v1/auth/change-password"),
        QHttpServerRequest::Method::Post,
        [this](const QHttpServerRequest& request) {
            return changePasswordResponse(request);
        }
    );

    if (catalogRepository != nullptr) {
        catalogService_ = std::make_shared<manage::data::CatalogService>(
            std::move(catalogRepository)
        );
        registerCatalogRoutes(server_, catalogService_, authService_);
    }

    BomRoutes::registerRoutes(server_, bomService, authService_);
    QuoteRoutes::registerRoutes(server_, quoteLifecycle, authService_);
    UserRoutes::registerRoutes(server_, userManagementService, authService_);
    StatisticsRoutes::registerRoutes(server_, statisticsRepository, authService_);
    MaterialBatchRoutes::registerRoutes(server_, materialBatchService, authService_);
}

quint16 ApiServer::listen(const QHostAddress& address, quint16 port) {
    if (!tcpServer_->listen(address, port)) {
        return 0;
    }
    if (!server_.bind(tcpServer_)) {
        tcpServer_->close();
        return 0;
    }
    return tcpServer_->serverPort();
}

QHttpServerResponse ApiServer::healthResponse() const {
    QJsonArray drivers;
    for (const auto& driver : manage::data::DatabaseConnection::availableDrivers()) {
        drivers.append(driver);
    }

    const QJsonObject database{
        {QStringLiteral("driver"), QStringLiteral("QMYSQL")},
        {
            QStringLiteral("driverAvailable"),
            manage::data::DatabaseConnection::mysqlDriverAvailable()
        },
        {QStringLiteral("availableDrivers"), drivers},
    };

    return QHttpServerResponse(QJsonObject{
        {QStringLiteral("status"), QStringLiteral("ok")},
        {QStringLiteral("service"), QStringLiteral("manage-server")},
        {QStringLiteral("database"), database},
    });
}

QHttpServerResponse ApiServer::calculateQuoteResponse(
    const QHttpServerRequest& request
) const {
    if (auto failure = HttpAuthorization::require(
            request,
            authService_,
            {
                manage::auth::UserRole::Admin,
                manage::auth::UserRole::Quoter,
            }
        )) {
        return std::move(*failure);
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(request.body(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return errorResponse(
            QStringLiteral("invalid_json"),
            QStringLiteral("request body must be a JSON object")
        );
    }

    const auto object = document.object();
    const auto linesValue = object.value(QStringLiteral("lines"));
    if (!linesValue.isArray()) {
        return errorResponse(
            QStringLiteral("invalid_request"),
            QStringLiteral("lines must be an array")
        );
    }

    manage::domain::QuoteCalculationInput input;
    const auto lines = linesValue.toArray();
    input.lines.reserve(static_cast<std::size_t>(lines.size()));

    for (qsizetype index = 0; index < lines.size(); ++index) {
        if (!lines.at(index).isObject()) {
            return errorResponse(
                QStringLiteral("invalid_request"),
                QStringLiteral("each line must be an object")
            );
        }

        const auto lineObject = lines.at(index).toObject();
        qint64 quantityMicros = 0;
        qint64 unitPriceCents = 0;
        if (!readInteger(
                lineObject,
                QStringLiteral("quantityMicros"),
                quantityMicros,
                true
            ) ||
            !readInteger(
                lineObject,
                QStringLiteral("unitPriceCents"),
                unitPriceCents,
                true
            )) {
            return errorResponse(
                QStringLiteral("invalid_request"),
                QStringLiteral(
                    "quantityMicros and unitPriceCents must be safe JSON integers"
                )
            );
        }

        input.lines.push_back({
            lineObject.value(QStringLiteral("materialCode"))
                .toString()
                .toStdString(),
            quantityMicros,
            manage::domain::Money::fromCents(unitPriceCents),
        });
    }

    qint64 freightCents = 0;
    qint64 otherFeesCents = 0;
    qint64 markupBasisPoints = 0;
    qint64 taxBasisPoints = 0;
    if (!readInteger(
            object,
            QStringLiteral("freightCents"),
            freightCents,
            false
        ) ||
        !readInteger(
            object,
            QStringLiteral("otherFeesCents"),
            otherFeesCents,
            false
        ) ||
        !readInteger(
            object,
            QStringLiteral("markupBasisPoints"),
            markupBasisPoints,
            false
        ) ||
        !readInteger(
            object,
            QStringLiteral("taxBasisPoints"),
            taxBasisPoints,
            false
        ) ||
        markupBasisPoints < std::numeric_limits<std::int32_t>::min() ||
        markupBasisPoints > std::numeric_limits<std::int32_t>::max() ||
        taxBasisPoints < std::numeric_limits<std::int32_t>::min() ||
        taxBasisPoints > std::numeric_limits<std::int32_t>::max()) {
        return errorResponse(
            QStringLiteral("invalid_request"),
            QStringLiteral("fees and percentages must be safe JSON integers")
        );
    }

    input.freight = manage::domain::Money::fromCents(freightCents);
    input.otherFees = manage::domain::Money::fromCents(otherFeesCents);
    input.markupBasisPoints = static_cast<std::int32_t>(markupBasisPoints);
    input.taxBasisPoints = static_cast<std::int32_t>(taxBasisPoints);

    try {
        const auto result = manage::domain::calculateQuote(input);
        QJsonArray resultLines;
        for (const auto& line : result.lines) {
            QJsonObject lineObject;
            lineObject.insert(
                QStringLiteral("materialCode"),
                QString::fromStdString(line.materialCode)
            );
            lineObject.insert(
                QStringLiteral("subtotalCents"),
                jsonInteger(line.subtotal.cents())
            );
            resultLines.append(lineObject);
        }

        QJsonObject response;
        response.insert(QStringLiteral("lines"), resultLines);
        response.insert(
            QStringLiteral("materialCostCents"),
            jsonInteger(result.materialCost.cents())
        );
        response.insert(
            QStringLiteral("priceBeforeTaxCents"),
            jsonInteger(result.priceBeforeTax.cents())
        );
        response.insert(
            QStringLiteral("taxAmountCents"),
            jsonInteger(result.taxAmount.cents())
        );
        response.insert(
            QStringLiteral("priceWithTaxCents"),
            jsonInteger(result.priceWithTax.cents())
        );
        return QHttpServerResponse(response);
    } catch (const manage::domain::QuoteCalculationError& error) {
        QJsonObject response{
            {QStringLiteral("error"), domainErrorCode(error.code())},
            {
                QStringLiteral("message"),
                QString::fromUtf8(error.what())
            },
        };
        if (error.lineIndex().has_value()) {
            response.insert(
                QStringLiteral("lineIndex"),
                jsonInteger(static_cast<std::int64_t>(*error.lineIndex()))
            );
        }
        return QHttpServerResponse(response, StatusCode::BadRequest);
    }
}

QHttpServerResponse ApiServer::bootstrapResponse(
    const QHttpServerRequest& request
) const {
    if (!authService_) {
        return errorResponse(
            QStringLiteral("auth_unavailable"),
            QStringLiteral("authentication service is unavailable"),
            StatusCode::ServiceUnavailable
        );
    }

    QJsonObject object;
    QHttpServerResponse failureResponse(StatusCode::BadRequest);
    if (!parseObjectBody(request, &object, &failureResponse)) {
        return failureResponse;
    }
    if (!object.value(QStringLiteral("password")).isString() ||
        (object.contains(QStringLiteral("displayName")) &&
         !object.value(QStringLiteral("displayName")).isString())) {
        return errorResponse(
            QStringLiteral("invalid_request"),
            QStringLiteral("password must be a string and displayName must be a string")
        );
    }

    const auto result = authService_->bootstrapAdministrator(
        object.value(QStringLiteral("password")).toString(),
        object.value(QStringLiteral("displayName"))
            .toString(QStringLiteral("初始管理员"))
    );
    if (!result.succeeded()) {
        return HttpAuthorization::errorResponse(result);
    }
    return QHttpServerResponse(
        QJsonObject{{QStringLiteral("user"), userJson(result.session.user)}},
        StatusCode::Created
    );
}

QHttpServerResponse ApiServer::loginResponse(
    const QHttpServerRequest& request
) const {
    if (!authService_) {
        return errorResponse(
            QStringLiteral("auth_unavailable"),
            QStringLiteral("authentication service is unavailable"),
            StatusCode::ServiceUnavailable
        );
    }

    QJsonObject object;
    QHttpServerResponse failureResponse(StatusCode::BadRequest);
    if (!parseObjectBody(request, &object, &failureResponse)) {
        return failureResponse;
    }
    if (!object.value(QStringLiteral("username")).isString() ||
        !object.value(QStringLiteral("password")).isString()) {
        return errorResponse(
            QStringLiteral("invalid_request"),
            QStringLiteral("username and password must be strings")
        );
    }

    const auto result = authService_->login(
        object.value(QStringLiteral("username")).toString(),
        object.value(QStringLiteral("password")).toString()
    );
    if (!result.succeeded()) {
        return HttpAuthorization::errorResponse(result);
    }
    return QHttpServerResponse(QJsonObject{
        {QStringLiteral("accessToken"), result.session.accessToken},
        {QStringLiteral("tokenType"), QStringLiteral("Bearer")},
        {
            QStringLiteral("expiresAt"),
            result.session.expiresAtUtc.toString(Qt::ISODateWithMs)
        },
        {QStringLiteral("user"), userJson(result.session.user)},
    });
}

QHttpServerResponse ApiServer::logoutResponse(
    const QHttpServerRequest& request
) const {
    if (!authService_) {
        return errorResponse(
            QStringLiteral("auth_unavailable"),
            QStringLiteral("authentication service is unavailable"),
            StatusCode::ServiceUnavailable
        );
    }
    const auto result = authService_->logout(
        HttpAuthorization::bearerToken(request)
    );
    if (!result.succeeded()) {
        return HttpAuthorization::errorResponse(result);
    }
    return QHttpServerResponse(QJsonObject{
        {QStringLiteral("status"), QStringLiteral("logged_out")},
    });
}

QHttpServerResponse ApiServer::meResponse(
    const QHttpServerRequest& request
) const {
    if (!authService_) {
        return errorResponse(
            QStringLiteral("auth_unavailable"),
            QStringLiteral("authentication service is unavailable"),
            StatusCode::ServiceUnavailable
        );
    }
    const auto result = authService_->currentUser(
        HttpAuthorization::bearerToken(request)
    );
    if (!result.succeeded()) {
        return HttpAuthorization::errorResponse(result);
    }
    return QHttpServerResponse(QJsonObject{
        {QStringLiteral("user"), userJson(result.session.user)},
        {
            QStringLiteral("expiresAt"),
            result.session.expiresAtUtc.toString(Qt::ISODateWithMs)
        },
    });
}

QHttpServerResponse ApiServer::changePasswordResponse(
    const QHttpServerRequest& request
) const {
    if (!authService_) {
        return errorResponse(
            QStringLiteral("auth_unavailable"),
            QStringLiteral("authentication service is unavailable"),
            StatusCode::ServiceUnavailable
        );
    }

    QJsonObject object;
    QHttpServerResponse failureResponse(StatusCode::BadRequest);
    if (!parseObjectBody(request, &object, &failureResponse)) {
        return failureResponse;
    }
    if (!object.value(QStringLiteral("currentPassword")).isString() ||
        !object.value(QStringLiteral("newPassword")).isString()) {
        return errorResponse(
            QStringLiteral("invalid_request"),
            QStringLiteral("currentPassword and newPassword must be strings")
        );
    }

    const auto result = authService_->changePassword(
        HttpAuthorization::bearerToken(request),
        object.value(QStringLiteral("currentPassword")).toString(),
        object.value(QStringLiteral("newPassword")).toString()
    );
    if (!result.succeeded()) {
        return HttpAuthorization::errorResponse(result);
    }
    return QHttpServerResponse(QJsonObject{
        {QStringLiteral("status"), QStringLiteral("password_changed")},
        {QStringLiteral("user"), userJson(result.session.user)},
    });
}

} // namespace manage::server
