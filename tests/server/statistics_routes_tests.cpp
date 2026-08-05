#include "manage/server/api_server.h"

#include "manage/auth/auth_service.h"
#include "manage/data/statistics_repository.h"
#include "support/fake_user_repository.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {

void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

class FakeStatisticsRepository final : public manage::data::StatisticsRepository {
public:
    manage::data::QuoteResult<manage::data::StatisticsReport> query(manage::data::StatisticsFilter filter) override {
        ++calls;
        lastFilter = filter;
        if (fail) return manage::data::QuoteResult<manage::data::StatisticsReport>::failure(manage::data::QuoteErrorCode::Infrastructure, QStringLiteral("statistics failed"));
        manage::data::StatisticsReport report;
        report.filter = filter;
        report.summary = {4, 50'000, 12'500, 2, 1, 7'500};
        report.byMonth.push_back({QStringLiteral("2026-08"), QStringLiteral("2026-08"), std::nullopt, 4, 50'000});
        report.byCustomer.push_back({QStringLiteral("44"), QStringLiteral("快照客户"), 44, 3, 40'000});
        report.byMaterialCategory.push_back({QStringLiteral("钢材"), QStringLiteral("钢材"), std::nullopt, 2, 20'000});
        return manage::data::QuoteResult<manage::data::StatisticsReport>::success(std::move(report));
    }
    int calls{};
    bool fail{};
    manage::data::StatisticsFilter lastFilter;
};

struct Response { int status{}; QByteArray body; };

Response wait(QNetworkReply* reply) {
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, [&] { reply->abort(); loop.quit(); });
    timer.start(5'000);
    if (!reply->isFinished()) loop.exec();
    require(reply->isFinished(), "statistics HTTP timeout");
    return {reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), reply->readAll()};
}

QNetworkRequest request(quint16 port, const QString& path, const QByteArray& token = {}) {
    QNetworkRequest result(QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(port).arg(path)));
    if (!token.isEmpty()) result.setRawHeader("Authorization", "Bearer " + token);
    return result;
}

QJsonObject json(const Response& response) { return QJsonDocument::fromJson(response.body).object(); }

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    try {
        auto users = std::make_shared<manage::tests::FakeUserRepository>();
        auto auth = std::make_shared<manage::auth::AuthService>(
            users,
            manage::auth::PasswordHasher(manage::auth::PasswordHasher::kMinimumIterations)
        );
        require(auth->bootstrapAdministrator(QStringLiteral("Statistics Password 1"), QStringLiteral("统计管理员")).succeeded(), "bootstrap");
        auto login = auth->login(QStringLiteral("admin"), QStringLiteral("Statistics Password 1"));
        require(login.succeeded(), "login");
        require(auth->changePassword(login.session.accessToken, QStringLiteral("Statistics Password 1"), QStringLiteral("Statistics Password 2")).succeeded(), "change password");
        login = auth->login(QStringLiteral("admin"), QStringLiteral("Statistics Password 2"));
        require(login.succeeded(), "active login");
        const auto token = login.session.accessToken.toLatin1();

        FakeStatisticsRepository statistics;
        manage::server::ApiServer server(auth, nullptr, nullptr, nullptr, &statistics);
        const auto port = server.listen(QHostAddress::LocalHost, 0);
        require(port != 0, "listen");
        QNetworkAccessManager network;

        auto response = wait(network.get(request(port, QStringLiteral("/api/v1/statistics?startDate=2026-08-01&endDate=2026-08-31"))));
        require(response.status == 401, "anonymous must be rejected");

        for (const auto role : {manage::auth::UserRole::Admin, manage::auth::UserRole::Quoter, manage::auth::UserRole::Viewer}) {
            users->setRole(role);
            response = wait(network.get(request(port, QStringLiteral("/api/v1/statistics?startDate=2026-08-01&endDate=2026-08-31&customerId=44&status=issued"), token)));
            require(response.status == 200, "all three roles receive read-only statistics");
        }
        require(statistics.lastFilter.startDate == QDate(2026, 8, 1), "start date forwarded");
        require(statistics.lastFilter.endDate == QDate(2026, 8, 31), "end date forwarded");
        require(statistics.lastFilter.customerId == 44, "customer forwarded");
        require(statistics.lastFilter.status == manage::data::QuoteStatus::Issued, "status forwarded");
        auto object = json(response);
        auto summary = object.value(QStringLiteral("summary")).toObject();
        require(summary.value(QStringLiteral("totalAmountCents")).toInteger() == 50'000, "integer cents JSON");
        require(summary.value(QStringLiteral("publishedRateBasisPoints")).toInt() == 7'500, "published rate JSON");
        require(!object.contains(QStringLiteral("successRate")), "must not expose sales success rate");
        require(object.value(QStringLiteral("byMonth")).toArray().size() == 1, "month dimension");
        require(object.value(QStringLiteral("byCustomer")).toArray().first().toObject().value(QStringLiteral("entityId")).toInteger() == 44, "customer dimension");
        require(object.value(QStringLiteral("byMaterialCategory")).toArray().first().toObject().value(QStringLiteral("totalAmountCents")).toInteger() == 20'000, "category dimension");

        const auto calls = statistics.calls;
        response = wait(network.get(request(port, QStringLiteral("/api/v1/statistics?startDate=bad&endDate=2026-08-31"), token)));
        require(response.status == 400 && statistics.calls == calls, "bad date rejected before data layer");
        response = wait(network.get(request(port, QStringLiteral("/api/v1/statistics?startDate=2026-08-01&endDate=2026-08-31&status=won"), token)));
        require(response.status == 400, "unsupported success-like status rejected");
        statistics.fail = true;
        response = wait(network.get(request(port, QStringLiteral("/api/v1/statistics?startDate=2026-08-01&endDate=2026-08-31"), token)));
        require(response.status == 500, "repository error mapping");
        std::cout << "[PASS] statistics filters, read roles, JSON and errors\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] statistics routes: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
