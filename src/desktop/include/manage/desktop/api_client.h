#pragma once

#include <QJsonObject>
#include <QNetworkReply>
#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;

namespace manage::desktop {

enum class ApiErrorKind {
    None,
    InvalidConfiguration,
    Network,
    Http,
    InvalidResponse,
};

struct ApiError final {
    ApiErrorKind kind{ApiErrorKind::None};
    int httpStatus{};
    QNetworkReply::NetworkError networkError{QNetworkReply::NoError};
    QString code;
    QString message;
};

struct ApiResponse final {
    int httpStatus{};
    QJsonObject body;
    ApiError error;

    [[nodiscard]] bool succeeded() const noexcept {
        return error.kind == ApiErrorKind::None;
    }
};

struct ApiSession final {
    QString accessToken;
    QString expiresAt;
    QJsonObject user;

    [[nodiscard]] bool authenticated() const noexcept {
        return !accessToken.isEmpty();
    }
};

class ApiClient final : public QObject {
    Q_OBJECT

public:
    using Callback = std::function<void(ApiResponse)>;

    explicit ApiClient(
        QUrl baseUrl,
        QObject* parent = nullptr
    );
    ApiClient(
        QUrl baseUrl,
        QNetworkAccessManager* networkManager,
        QObject* parent = nullptr
    );

    [[nodiscard]] QUrl baseUrl() const;
    [[nodiscard]] bool setBaseUrl(const QUrl& baseUrl);
    [[nodiscard]] const ApiSession& session() const noexcept;
    [[nodiscard]] bool isAuthenticated() const noexcept;
    void clearSession();

    QNetworkReply* get(const QString& path, Callback callback);
    QNetworkReply* post(
        const QString& path,
        const QJsonObject& body,
        Callback callback
    );
    QNetworkReply* put(
        const QString& path,
        const QJsonObject& body,
        Callback callback
    );
    QNetworkReply* patch(
        const QString& path,
        const QJsonObject& body,
        Callback callback
    );

    QNetworkReply* bootstrap(
        const QString& password,
        const QString& displayName,
        Callback callback
    );
    QNetworkReply* login(
        const QString& username,
        const QString& password,
        Callback callback
    );
    QNetworkReply* logout(Callback callback);
    QNetworkReply* me(Callback callback);
    QNetworkReply* changePassword(
        const QString& currentPassword,
        const QString& newPassword,
        Callback callback
    );

signals:
    void sessionChanged(bool authenticated);

private:
    enum class Method { Get, Post, Put, Patch };

    QNetworkReply* send(
        Method method,
        const QString& path,
        const QJsonObject& body,
        Callback callback
    );
    [[nodiscard]] QUrl endpointUrl(const QString& path) const;
    void setSession(ApiSession session);
    void completeLater(Callback callback, ApiResponse response);

    QUrl baseUrl_;
    QNetworkAccessManager* networkManager_{};
    ApiSession session_;
};

} // namespace manage::desktop
