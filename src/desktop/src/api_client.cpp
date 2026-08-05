#include "manage/desktop/api_client.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QTimer>

#include <utility>

namespace manage::desktop {
namespace {

bool validBaseUrl(const QUrl& url) {
    const auto scheme = url.scheme().toLower();
    return url.isValid() && (scheme == QStringLiteral("http") ||
                             scheme == QStringLiteral("https")) &&
           !url.host().isEmpty() && url.userInfo().isEmpty() &&
           !url.hasQuery() && !url.hasFragment();
}

ApiResponse invalidConfiguration(const QString& message) {
    ApiResponse response;
    response.error.kind = ApiErrorKind::InvalidConfiguration;
    response.error.code = QStringLiteral("invalid_configuration");
    response.error.message = message;
    return response;
}

} // namespace

ApiClient::ApiClient(QUrl baseUrl, QObject* parent)
    : QObject(parent),
      networkManager_(new QNetworkAccessManager(this)) {
    (void)setBaseUrl(baseUrl);
}

ApiClient::ApiClient(
    QUrl baseUrl,
    QNetworkAccessManager* networkManager,
    QObject* parent
)
    : QObject(parent), networkManager_(networkManager) {
    (void)setBaseUrl(baseUrl);
}

QUrl ApiClient::baseUrl() const {
    return baseUrl_;
}

bool ApiClient::setBaseUrl(const QUrl& baseUrl) {
    if (!validBaseUrl(baseUrl)) {
        return false;
    }
    const auto normalized = baseUrl.adjusted(
        QUrl::NormalizePathSegments | QUrl::StripTrailingSlash
    );
    if (!baseUrl_.isEmpty() && baseUrl_ != normalized) {
        clearSession();
    }
    baseUrl_ = normalized;
    return true;
}

const ApiSession& ApiClient::session() const noexcept {
    return session_;
}

bool ApiClient::isAuthenticated() const noexcept {
    return session_.authenticated();
}

void ApiClient::clearSession() {
    if (!session_.authenticated() && session_.user.isEmpty() &&
        session_.expiresAt.isEmpty()) {
        return;
    }
    session_ = {};
    emit sessionChanged(false);
}

QNetworkReply* ApiClient::get(const QString& path, Callback callback) {
    return send(Method::Get, path, {}, std::move(callback));
}

QNetworkReply* ApiClient::post(
    const QString& path,
    const QJsonObject& body,
    Callback callback
) {
    return send(Method::Post, path, body, std::move(callback));
}

QNetworkReply* ApiClient::put(
    const QString& path,
    const QJsonObject& body,
    Callback callback
) {
    return send(Method::Put, path, body, std::move(callback));
}

QNetworkReply* ApiClient::patch(
    const QString& path,
    const QJsonObject& body,
    Callback callback
) {
    return send(Method::Patch, path, body, std::move(callback));
}

QNetworkReply* ApiClient::bootstrap(
    const QString& password,
    const QString& displayName,
    Callback callback
) {
    return post(
        QStringLiteral("/api/v1/auth/bootstrap"),
        QJsonObject{
            {QStringLiteral("password"), password},
            {QStringLiteral("displayName"), displayName},
        },
        std::move(callback)
    );
}

QNetworkReply* ApiClient::login(
    const QString& username,
    const QString& password,
    Callback callback
) {
    return post(
        QStringLiteral("/api/v1/auth/login"),
        QJsonObject{
            {QStringLiteral("username"), username},
            {QStringLiteral("password"), password},
        },
        [this, callback = std::move(callback)](ApiResponse response) mutable {
            if (response.succeeded()) {
                const auto token = response.body
                                       .value(QStringLiteral("accessToken"))
                                       .toString();
                const auto user = response.body
                                      .value(QStringLiteral("user"))
                                      .toObject();
                if (token.isEmpty() || user.isEmpty()) {
                    response.error.kind = ApiErrorKind::InvalidResponse;
                    response.error.code = QStringLiteral("invalid_response");
                    response.error.message = QStringLiteral(
                        "login response is missing accessToken or user"
                    );
                } else {
                    setSession(ApiSession{
                        token,
                        response.body.value(QStringLiteral("expiresAt"))
                            .toString(),
                        user,
                    });
                }
            }
            if (callback) {
                callback(std::move(response));
            }
        }
    );
}

QNetworkReply* ApiClient::logout(Callback callback) {
    return post(
        QStringLiteral("/api/v1/auth/logout"),
        {},
        [this, callback = std::move(callback)](ApiResponse response) mutable {
            if (response.succeeded()) {
                clearSession();
            }
            if (callback) {
                callback(std::move(response));
            }
        }
    );
}

QNetworkReply* ApiClient::me(Callback callback) {
    return get(
        QStringLiteral("/api/v1/auth/me"),
        [this, callback = std::move(callback)](ApiResponse response) mutable {
            if (response.succeeded() && isAuthenticated()) {
                session_.user = response.body.value(QStringLiteral("user"))
                                    .toObject();
                session_.expiresAt = response.body
                                         .value(QStringLiteral("expiresAt"))
                                         .toString();
                emit sessionChanged(true);
            }
            if (callback) {
                callback(std::move(response));
            }
        }
    );
}

QNetworkReply* ApiClient::changePassword(
    const QString& currentPassword,
    const QString& newPassword,
    Callback callback
) {
    return post(
        QStringLiteral("/api/v1/auth/change-password"),
        QJsonObject{
            {QStringLiteral("currentPassword"), currentPassword},
            {QStringLiteral("newPassword"), newPassword},
        },
        [this, callback = std::move(callback)](ApiResponse response) mutable {
            if (response.succeeded() && isAuthenticated()) {
                const auto user = response.body.value(QStringLiteral("user"))
                                      .toObject();
                if (!user.isEmpty()) {
                    session_.user = user;
                    emit sessionChanged(true);
                }
            }
            if (callback) {
                callback(std::move(response));
            }
        }
    );
}

QNetworkReply* ApiClient::send(
    Method method,
    const QString& path,
    const QJsonObject& body,
    Callback callback
) {
    if (!networkManager_) {
        completeLater(
            std::move(callback),
            invalidConfiguration(QStringLiteral("network manager is unavailable"))
        );
        return nullptr;
    }

    const auto url = endpointUrl(path);
    if (!url.isValid()) {
        completeLater(
            std::move(callback),
            invalidConfiguration(QStringLiteral("base URL or endpoint path is invalid"))
        );
        return nullptr;
    }

    QNetworkRequest request(url);
    request.setHeader(
        QNetworkRequest::ContentTypeHeader,
        QStringLiteral("application/json")
    );
    request.setRawHeader("Accept", "application/json");
    if (session_.authenticated()) {
        request.setRawHeader(
            "Authorization",
            "Bearer " + session_.accessToken.toUtf8()
        );
    }

    QNetworkReply* reply{};
    const auto payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    switch (method) {
    case Method::Get:
        reply = networkManager_->get(request);
        break;
    case Method::Post:
        reply = networkManager_->post(request, payload);
        break;
    case Method::Put:
        reply = networkManager_->put(request, payload);
        break;
    case Method::Patch:
        reply = networkManager_->sendCustomRequest(request, "PATCH", payload);
        break;
    }

    connect(
        reply,
        &QNetworkReply::finished,
        this,
        [this, reply, callback = std::move(callback)]() mutable {
            ApiResponse response;
            response.httpStatus = reply
                                      ->attribute(
                                          QNetworkRequest::HttpStatusCodeAttribute
                                      )
                                      .toInt();

            const auto payload = reply->readAll();
            QJsonParseError parseError;
            const auto document = QJsonDocument::fromJson(payload, &parseError);
            if (!payload.isEmpty() &&
                (parseError.error != QJsonParseError::NoError ||
                 !document.isObject())) {
                response.error.kind = ApiErrorKind::InvalidResponse;
                response.error.code = QStringLiteral("invalid_response");
                response.error.message = QStringLiteral(
                    "server response must be a JSON object"
                );
            } else if (document.isObject()) {
                response.body = document.object();
            }

            if (response.error.kind == ApiErrorKind::None &&
                response.httpStatus >= 400) {
                response.error.kind = ApiErrorKind::Http;
                response.error.httpStatus = response.httpStatus;
                response.error.code = response.body
                                          .value(QStringLiteral("error"))
                                          .toString(QStringLiteral("http_error"));
                response.error.message = response.body
                                             .value(QStringLiteral("message"))
                                             .toString(reply->errorString());
            } else if (response.error.kind == ApiErrorKind::None &&
                       reply->error() != QNetworkReply::NoError) {
                response.error.kind = ApiErrorKind::Network;
                response.error.networkError = reply->error();
                response.error.code = QStringLiteral("network_error");
                response.error.message = reply->errorString();
            }

            if (response.httpStatus == 401) {
                clearSession();
            }
            reply->deleteLater();
            if (callback) {
                callback(std::move(response));
            }
        }
    );
    return reply;
}

QUrl ApiClient::endpointUrl(const QString& path) const {
    if (!validBaseUrl(baseUrl_)) {
        return {};
    }
    const QUrl relative(path);
    if (!relative.isValid() || !relative.isRelative() ||
        relative.hasFragment()) {
        return {};
    }

    auto base = baseUrl_;
    auto basePath = base.path();
    if (!basePath.endsWith(QLatin1Char('/'))) {
        basePath.append(QLatin1Char('/'));
    }
    base.setPath(basePath);

    auto relativePath = path;
    while (relativePath.startsWith(QLatin1Char('/'))) {
        relativePath.remove(0, 1);
    }
    return base.resolved(QUrl(relativePath));
}

void ApiClient::setSession(ApiSession session) {
    session_ = std::move(session);
    emit sessionChanged(session_.authenticated());
}

void ApiClient::completeLater(Callback callback, ApiResponse response) {
    QTimer::singleShot(
        0,
        this,
        [callback = std::move(callback), response = std::move(response)]() mutable {
            if (callback) {
                callback(std::move(response));
            }
        }
    );
}

} // namespace manage::desktop
