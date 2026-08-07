#include "manage/desktop/bom_quote_widget.h"
#include "manage/desktop/catalog_widget.h"
#include "manage/desktop/excel_tools_widget.h"
#include "manage/desktop/main_window.h"
#include "manage/desktop/process_step_widget.h"
#include "manage/desktop/quote_management_widget.h"
#include "manage/desktop/statistics_widget.h"
#include "manage/desktop/task_dispatch_widget.h"
#include "manage/desktop/user_management_widget.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QTimer>
#include <QUrl>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("manage-desktop"));
    QApplication::setApplicationVersion(QStringLiteral(MANAGE_VERSION));

    // 全局调大列表项/按钮/标签页的点击区域，避免小窗口下难以点选。
    application.setStyleSheet(QStringLiteral(
        "QTableView::item, QTableWidget::item,"
        "QListView::item, QListWidget::item {"
        "    min-height: 28px; padding: 4px;"
        "}"
        "QPushButton { min-height: 28px; padding: 4px 12px; }"
        "QTabBar::tab { min-height: 32px; padding: 6px 14px; }"
    ));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Desktop client for the quotation management system")
    );
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption apiUrlOption(
        QStringLiteral("api-url"),
        QStringLiteral("Base URL of the local or LAN REST API"),
        QStringLiteral("url"),
        QStringLiteral("http://127.0.0.1:18080")
    );
    const QCommandLineOption smokeTestOption(
        QStringLiteral("smoke-test"),
        QStringLiteral("Open the main window offscreen and exit immediately")
    );
    parser.addOption(apiUrlOption);
    parser.addOption(smokeTestOption);
    parser.process(application);

    const QUrl apiUrl(parser.value(apiUrlOption));
    const auto explicitPort = apiUrl.port(-1);
    if (!apiUrl.isValid() || apiUrl.scheme() != QStringLiteral("http") ||
        apiUrl.host().isEmpty() || explicitPort == 0 ||
        !apiUrl.userInfo().isEmpty() || !apiUrl.query().isEmpty() ||
        !apiUrl.fragment().isEmpty()) {
        qCritical(
            "--api-url must be an http URL without credentials, query or fragment"
        );
        return 2;
    }

    // The default remains localhost. Accepting a LAN hostname/IP here lets a
    // second Windows PC use the same API without ever exposing MySQL itself.

    manage::desktop::MainWindow window(apiUrl);
    window.addModuleTab(
        QStringLiteral("客户与物料"),
        new manage::desktop::CatalogWidget(window.apiClient())
    );
    window.addModuleTab(
        QStringLiteral("BOM 与报价"),
        new manage::desktop::BomQuoteWidget(window.apiClient())
    );
    window.addModuleTab(
        QStringLiteral("任务派发"),
        new manage::desktop::TaskDispatchWidget(window.apiClient())
    );
    window.addModuleTab(
        QStringLiteral("报价管理"),
        new manage::desktop::QuoteManagementWidget(window.apiClient())
    );
    window.addModuleTab(
        QStringLiteral("工序与工时"),
        new manage::desktop::ProcessStepWidget(window.apiClient())
    );
    window.addModuleTab(
        QStringLiteral("Excel 工具"),
        new manage::desktop::ExcelToolsWidget(window.apiClient())
    );
    window.addModuleTab(
        QStringLiteral("统计分析"),
        new manage::desktop::StatisticsWidget(window.apiClient())
    );
    window.addModuleTab(
        QStringLiteral("用户管理"),
        new manage::desktop::UserManagementWidget(window.apiClient())
    );
    window.show();

    if (parser.isSet(smokeTestOption)) {
        QTimer::singleShot(100, &application, &QApplication::quit);
    }

    return application.exec();
}
