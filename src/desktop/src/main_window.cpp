#include "manage/desktop/main_window.h"

#include <QFont>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <utility>

namespace manage::desktop {

MainWindow::MainWindow(QUrl apiBaseUrl, QWidget* parent)
    : QMainWindow(parent), apiBaseUrl_(std::move(apiBaseUrl)) {
    setWindowTitle(QStringLiteral("本地报价管理系统"));
    resize(860, 540);

    auto* centralWidget = new QWidget(this);
    auto* layout = new QVBoxLayout(centralWidget);
    layout->setContentsMargins(48, 40, 48, 40);
    layout->setSpacing(18);

    auto* title = new QLabel(QStringLiteral("本地报价管理系统"), centralWidget);
    auto titleFont = title->font();
    titleFont.setPointSize(24);
    titleFont.setBold(true);
    title->setFont(titleFont);

    auto* subtitle = new QLabel(
        QStringLiteral(
            "Qt Widgets 客户端已启动。所有报价核算均由本机 REST API 完成。"
        ),
        centralWidget
    );
    subtitle->setWordWrap(true);

    auto* moduleSummary = new QLabel(
        QStringLiteral(
            "当前模块\n"
            "• 本地 API 健康检查\n"
            "• 统一报价核算接口\n"
            "• MySQL 数据连接边界\n\n"
            "物料、客户和报价编辑界面将在后续业务模块中接入。"
        ),
        centralWidget
    );
    moduleSummary->setWordWrap(true);
    moduleSummary->setStyleSheet(QStringLiteral(
        "QLabel { background: #f4f7fb; border: 1px solid #dce4ee; "
        "border-radius: 10px; padding: 20px; color: #243447; }"
    ));

    statusLabel_ = new QLabel(QStringLiteral("正在检查本地 API…"), centralWidget);
    statusLabel_->setWordWrap(true);

    retryButton_ = new QPushButton(QStringLiteral("重新检查服务"), centralWidget);
    retryButton_->setFixedWidth(150);
    connect(
        retryButton_,
        &QPushButton::clicked,
        this,
        [this]() { refreshServerStatus(); }
    );

    layout->addWidget(title);
    layout->addWidget(subtitle);
    layout->addWidget(moduleSummary);
    layout->addStretch();
    layout->addWidget(statusLabel_);
    layout->addWidget(retryButton_);

    setCentralWidget(centralWidget);
    QTimer::singleShot(0, this, [this]() { refreshServerStatus(); });
}

void MainWindow::refreshServerStatus() {
    retryButton_->setEnabled(false);
    statusLabel_->setText(QStringLiteral("正在连接 127.0.0.1 上的本地 API…"));

    const auto healthUrl = apiBaseUrl_.resolved(
        QUrl(QStringLiteral("/api/v1/health"))
    );
    auto* reply = networkManager_.get(QNetworkRequest(healthUrl));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        retryButton_->setEnabled(true);

        if (reply->error() != QNetworkReply::NoError) {
            statusLabel_->setText(
                QStringLiteral("本地 API 未连接：%1").arg(reply->errorString())
            );
            reply->deleteLater();
            return;
        }

        const auto document = QJsonDocument::fromJson(reply->readAll());
        const auto database = document.object()
                                  .value(QStringLiteral("database"))
                                  .toObject();
        const auto mysqlAvailable = database
                                        .value(QStringLiteral("driverAvailable"))
                                        .toBool();
        statusLabel_->setText(
            mysqlAvailable
                ? QStringLiteral("本地 API 正常，QMYSQL 驱动可用。")
                : QStringLiteral(
                      "本地 API 正常；QMYSQL 驱动尚未安装，数据库连接暂不可用。"
                  )
        );
        reply->deleteLater();
    });
}

} // namespace manage::desktop
