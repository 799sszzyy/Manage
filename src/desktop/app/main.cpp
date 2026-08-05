#include "manage/desktop/bom_quote_widget.h"
#include "manage/desktop/catalog_widget.h"
#include "manage/desktop/main_window.h"
#include "manage/desktop/quote_management_widget.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QTimer>
#include <QUrl>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("manage-desktop"));
    QApplication::setApplicationVersion(QStringLiteral("0.4.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Desktop client for the quotation management system")
    );
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption apiUrlOption(
        QStringLiteral("api-url"),
        QStringLiteral("Base URL of the local REST API"),
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
    if (!apiUrl.isValid() || apiUrl.scheme() != QStringLiteral("http") ||
        apiUrl.host() != QStringLiteral("127.0.0.1")) {
        qCritical("--api-url must be an http://127.0.0.1 URL");
        return 2;
    }

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
        QStringLiteral("报价管理"),
        new manage::desktop::QuoteManagementWidget(window.apiClient())
    );
    window.show();

    if (parser.isSet(smokeTestOption)) {
        QTimer::singleShot(100, &application, &QApplication::quit);
    }

    return application.exec();
}
