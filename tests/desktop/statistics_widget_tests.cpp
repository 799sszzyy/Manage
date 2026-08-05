#include "manage/desktop/api_client.h"
#include "manage/desktop/statistics_widget.h"

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateEdit>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponse>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTcpServer>
#include <QThread>
#include <QUrlQuery>

#include <cstdlib>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

bool waitUntil(const std::function<bool()>& predicate) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < 5'000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(2);
    }
    return predicate();
}

template<typename T>
T* child(QObject& root, const char* name) {
    auto* result = root.findChild<T*>(QString::fromLatin1(name));
    require(result != nullptr, name);
    return result;
}

class TestApi final {
public:
    TestApi() {
        server.route(QStringLiteral("/api/v1/statistics"), QHttpServerRequest::Method::Get,
            [this](const QHttpServerRequest& request) {
                ++getCalls;
                query = QUrlQuery(request.url());
                return QHttpServerResponse(QJsonObject{
                    {QStringLiteral("summary"), QJsonObject{
                        {QStringLiteral("quoteCount"), 4},
                        {QStringLiteral("totalAmountCents"), 50'000},
                        {QStringLiteral("averageAmountCents"), 12'500},
                        {QStringLiteral("issuedCount"), 2},
                        {QStringLiteral("voidCount"), 1},
                        {QStringLiteral("publishedRateBasisPoints"), 7'500},
                    }},
                    {QStringLiteral("byMonth"), QJsonArray{QJsonObject{
                        {QStringLiteral("label"), QStringLiteral("2026-08")},
                        {QStringLiteral("quoteCount"), 4},
                        {QStringLiteral("totalAmountCents"), 50'000},
                    }}},
                    {QStringLiteral("byCustomer"), QJsonArray{QJsonObject{
                        {QStringLiteral("label"), QStringLiteral("快照客户")},
                        {QStringLiteral("quoteCount"), 3},
                        {QStringLiteral("totalAmountCents"), 40'000},
                    }}},
                    {QStringLiteral("byMaterialCategory"), QJsonArray{QJsonObject{
                        {QStringLiteral("label"), QStringLiteral("钢材")},
                        {QStringLiteral("quoteCount"), 2},
                        {QStringLiteral("totalAmountCents"), 20'000},
                    }}},
                });
            });
        require(tcp.listen(QHostAddress::LocalHost, 0) && server.bind(&tcp), "mock listen");
    }
    QUrl baseUrl() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(tcp.serverPort())); }
    QHttpServer server;
    QTcpServer tcp;
    QUrlQuery query;
    int getCalls{};
};

} // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    try {
        TestApi api;
        manage::desktop::ApiClient client(api.baseUrl());
        manage::desktop::StatisticsWidget widget(&client);
        widget.show();

        child<QDateEdit>(widget, "statisticsStartDateEdit")->setDate(QDate(2026, 8, 1));
        child<QDateEdit>(widget, "statisticsEndDateEdit")->setDate(QDate(2026, 8, 31));
        child<QLineEdit>(widget, "statisticsCustomerIdEdit")->setText(QStringLiteral("44"));
        child<QComboBox>(widget, "statisticsStatusCombo")->setCurrentIndex(2);
        child<QPushButton>(widget, "statisticsRefreshButton")->click();

        require(waitUntil([&] { return api.getCalls == 1 && child<QPushButton>(widget, "statisticsRefreshButton")->isEnabled(); }), "statistics request completes");
        require(api.query.queryItemValue(QStringLiteral("startDate")) == QStringLiteral("2026-08-01"), "start filter");
        require(api.query.queryItemValue(QStringLiteral("endDate")) == QStringLiteral("2026-08-31"), "end filter");
        require(api.query.queryItemValue(QStringLiteral("customerId")) == QStringLiteral("44"), "customer filter");
        require(api.query.queryItemValue(QStringLiteral("status")) == QStringLiteral("issued"), "status filter");
        require(child<QLabel>(widget, "statisticsQuoteCountLabel")->text() == QStringLiteral("4"), "quote count display");
        require(child<QLabel>(widget, "statisticsTotalAmountLabel")->text() == QStringLiteral("¥500.00"), "integer cents display");
        require(child<QLabel>(widget, "statisticsAverageAmountLabel")->text() == QStringLiteral("¥125.00"), "average display");
        require(child<QLabel>(widget, "statisticsPublishedRateLabel")->text() == QStringLiteral("75.00%"), "published rate display");
        require(child<QTableWidget>(widget, "statisticsMonthTable")->item(0, 0)->text() == QStringLiteral("2026-08"), "month table");
        require(child<QTableWidget>(widget, "statisticsCustomerTable")->item(0, 0)->text() == QStringLiteral("快照客户"), "customer table");
        require(child<QTableWidget>(widget, "statisticsCategoryTable")->item(0, 2)->text() == QStringLiteral("¥200.00"), "category subtotal display");

        child<QDateEdit>(widget, "statisticsStartDateEdit")->setDate(QDate(2026, 9, 1));
        child<QDateEdit>(widget, "statisticsEndDateEdit")->setDate(QDate(2026, 8, 1));
        child<QPushButton>(widget, "statisticsRefreshButton")->click();
        QCoreApplication::processEvents();
        require(api.getCalls == 1, "invalid local range must not send request");
        require(child<QLabel>(widget, "statisticsMessageLabel")->text().contains(QStringLiteral("不能晚于")), "friendly validation");

        std::cout << "[PASS] statistics widget filters and read-only presentation\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] statistics widget: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
