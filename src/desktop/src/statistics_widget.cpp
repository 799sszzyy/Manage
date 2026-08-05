#include "manage/desktop/statistics_widget.h"

#include "manage/desktop/api_client.h"
#include "manage/excel/workbook_service.h"

#include <QComboBox>
#include <QDate>
#include <QDateEdit>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QIntValidator>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTabWidget>
#include <QUrlQuery>
#include <QVBoxLayout>

#include <utility>

namespace manage::desktop {
namespace {

QString money(qint64 cents) {
    return QStringLiteral("¥%1.%2")
        .arg(cents / 100)
        .arg(cents % 100, 2, 10, QLatin1Char('0'));
}

QString percent(int basisPoints) {
    return QStringLiteral("%1.%2%")
        .arg(basisPoints / 100)
        .arg(basisPoints % 100, 2, 10, QLatin1Char('0'));
}

QLabel* valueLabel(const char* name, QWidget* parent) {
    auto* label = new QLabel(QStringLiteral("—"), parent);
    label->setObjectName(QString::fromLatin1(name));
    auto font = label->font();
    font.setBold(true);
    label->setFont(font);
    return label;
}

QTableWidget* dimensionTable(const char* name, QWidget* parent) {
    auto* table = new QTableWidget(0, 3, parent);
    table->setObjectName(QString::fromLatin1(name));
    table->setHorizontalHeaderLabels({QStringLiteral("维度"), QStringLiteral("报价数"), QStringLiteral("金额")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    return table;
}

void fillTable(QTableWidget* table, const QJsonArray& rows) {
    table->setRowCount(rows.size());
    for (int rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
        const auto row = rows.at(rowIndex).toObject();
        table->setItem(rowIndex, 0, new QTableWidgetItem(row.value(QStringLiteral("label")).toString()));
        table->setItem(rowIndex, 1, new QTableWidgetItem(QString::number(row.value(QStringLiteral("quoteCount")).toInteger())));
        table->setItem(rowIndex, 2, new QTableWidgetItem(money(row.value(QStringLiteral("totalAmountCents")).toInteger())));
    }
}

std::vector<manage::excel::StatisticsRow> workbookRows(const QJsonArray& rows) {
    std::vector<manage::excel::StatisticsRow> result;
    result.reserve(static_cast<std::size_t>(rows.size()));
    for (const auto& value : rows) {
        const auto row = value.toObject();
        result.push_back({
            row.value(QStringLiteral("label")).toString(),
            row.value(QStringLiteral("quoteCount")).toInteger(),
            row.value(QStringLiteral("totalAmountCents")).toInteger(),
        });
    }
    return result;
}

QString friendlyError(const ApiResponse& response) {
    if (response.error.kind == ApiErrorKind::Network) return QStringLiteral("无法连接服务：%1").arg(response.error.message);
    if (response.error.code == QStringLiteral("forbidden")) return QStringLiteral("当前账号无权查看统计。管理员、报价员和查看员均应具有只读权限。");
    if (response.error.code == QStringLiteral("password_change_required")) return QStringLiteral("首次登录必须先修改临时密码。 ");
    if (response.error.code == QStringLiteral("invalid_request")) return QStringLiteral("筛选条件不正确：%1").arg(response.error.message);
    return response.error.message.isEmpty() ? QStringLiteral("统计查询失败。") : QStringLiteral("统计查询失败：%1").arg(response.error.message);
}

} // namespace

StatisticsWidget::StatisticsWidget(ApiClient* apiClient, QWidget* parent)
    : QWidget(parent), apiClient_(apiClient) {
    setObjectName(QStringLiteral("statisticsWidget"));
    auto* root = new QVBoxLayout(this);

    auto* title = new QLabel(QStringLiteral("报价统计分析"), this);
    auto font = title->font();
    font.setPointSize(18);
    font.setBold(true);
    title->setFont(font);
    root->addWidget(title);

    auto* filterBox = new QGroupBox(QStringLiteral("筛选条件（按报价创建日期）"), this);
    auto* filters = new QGridLayout(filterBox);
    startDateEdit_ = new QDateEdit(QDate(QDate::currentDate().year(), 1, 1), filterBox);
    startDateEdit_->setObjectName(QStringLiteral("statisticsStartDateEdit"));
    startDateEdit_->setCalendarPopup(true);
    startDateEdit_->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    endDateEdit_ = new QDateEdit(QDate::currentDate(), filterBox);
    endDateEdit_->setObjectName(QStringLiteral("statisticsEndDateEdit"));
    endDateEdit_->setCalendarPopup(true);
    endDateEdit_->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    customerIdEdit_ = new QLineEdit(filterBox);
    customerIdEdit_->setObjectName(QStringLiteral("statisticsCustomerIdEdit"));
    customerIdEdit_->setPlaceholderText(QStringLiteral("留空表示全部客户"));
    customerIdEdit_->setValidator(new QIntValidator(1, 2'000'000'000, customerIdEdit_));
    statusCombo_ = new QComboBox(filterBox);
    statusCombo_->setObjectName(QStringLiteral("statisticsStatusCombo"));
    statusCombo_->addItem(QStringLiteral("全部状态"), QString());
    statusCombo_->addItem(QStringLiteral("草稿"), QStringLiteral("draft"));
    statusCombo_->addItem(QStringLiteral("已发布"), QStringLiteral("issued"));
    statusCombo_->addItem(QStringLiteral("已作废"), QStringLiteral("void"));
    refreshButton_ = new QPushButton(QStringLiteral("查询统计"), filterBox);
    refreshButton_->setObjectName(QStringLiteral("statisticsRefreshButton"));
    exportButton_ = new QPushButton(QStringLiteral("导出 Excel"), filterBox);
    exportButton_->setObjectName(QStringLiteral("statisticsExportButton"));
    exportButton_->setEnabled(false);
    filters->addWidget(new QLabel(QStringLiteral("开始日期"), filterBox), 0, 0);
    filters->addWidget(startDateEdit_, 0, 1);
    filters->addWidget(new QLabel(QStringLiteral("结束日期"), filterBox), 0, 2);
    filters->addWidget(endDateEdit_, 0, 3);
    filters->addWidget(new QLabel(QStringLiteral("客户 ID"), filterBox), 1, 0);
    filters->addWidget(customerIdEdit_, 1, 1);
    filters->addWidget(new QLabel(QStringLiteral("报价状态"), filterBox), 1, 2);
    filters->addWidget(statusCombo_, 1, 3);
    filters->addWidget(refreshButton_, 1, 4);
    filters->addWidget(exportButton_, 1, 5);
    root->addWidget(filterBox);

    messageLabel_ = new QLabel(this);
    messageLabel_->setObjectName(QStringLiteral("statisticsMessageLabel"));
    messageLabel_->setWordWrap(true);
    root->addWidget(messageLabel_);

    auto* summaryBox = new QGroupBox(QStringLiteral("汇总（发布率不是成交率）"), this);
    auto* summary = new QFormLayout(summaryBox);
    quoteCountLabel_ = valueLabel("statisticsQuoteCountLabel", summaryBox);
    totalAmountLabel_ = valueLabel("statisticsTotalAmountLabel", summaryBox);
    averageAmountLabel_ = valueLabel("statisticsAverageAmountLabel", summaryBox);
    issuedCountLabel_ = valueLabel("statisticsIssuedCountLabel", summaryBox);
    voidCountLabel_ = valueLabel("statisticsVoidCountLabel", summaryBox);
    publishedRateLabel_ = valueLabel("statisticsPublishedRateLabel", summaryBox);
    summary->addRow(QStringLiteral("报价数"), quoteCountLabel_);
    summary->addRow(QStringLiteral("总金额"), totalAmountLabel_);
    summary->addRow(QStringLiteral("平均金额"), averageAmountLabel_);
    summary->addRow(QStringLiteral("当前已发布数"), issuedCountLabel_);
    summary->addRow(QStringLiteral("已作废数"), voidCountLabel_);
    summary->addRow(QStringLiteral("发布率（已发布 + 已作废）"), publishedRateLabel_);
    root->addWidget(summaryBox);

    auto* tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("statisticsDimensionTabs"));
    monthTable_ = dimensionTable("statisticsMonthTable", tabs);
    customerTable_ = dimensionTable("statisticsCustomerTable", tabs);
    categoryTable_ = dimensionTable("statisticsCategoryTable", tabs);
    tabs->addTab(monthTable_, QStringLiteral("按月份"));
    tabs->addTab(customerTable_, QStringLiteral("按客户"));
    tabs->addTab(categoryTable_, QStringLiteral("按物料类别（明细小计）"));
    root->addWidget(tabs, 1);

    connect(refreshButton_, &QPushButton::clicked, this, [this]() { refresh(); });
    connect(exportButton_, &QPushButton::clicked, this, [this]() { exportReport(); });
}

void StatisticsWidget::refresh() {
    if (!apiClient_) {
        messageLabel_->setText(QStringLiteral("统计服务客户端不可用。"));
        return;
    }
    if (startDateEdit_->date() > endDateEdit_->date()) {
        messageLabel_->setText(QStringLiteral("开始日期不能晚于结束日期。"));
        return;
    }
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("startDate"), startDateEdit_->date().toString(QStringLiteral("yyyy-MM-dd")));
    query.addQueryItem(QStringLiteral("endDate"), endDateEdit_->date().toString(QStringLiteral("yyyy-MM-dd")));
    if (!customerIdEdit_->text().trimmed().isEmpty()) query.addQueryItem(QStringLiteral("customerId"), customerIdEdit_->text().trimmed());
    const auto status = statusCombo_->currentData().toString();
    if (!status.isEmpty()) query.addQueryItem(QStringLiteral("status"), status);
    setBusy(true);
    messageLabel_->setText(QStringLiteral("正在查询……"));
    apiClient_->get(
        QStringLiteral("/api/v1/statistics?") + query.query(QUrl::FullyEncoded),
        [this](ApiResponse response) { showReport(response); }
    );
}

void StatisticsWidget::showReport(const ApiResponse& response) {
    setBusy(false);
    if (!response.succeeded()) {
        currentReport_ = {};
        exportButton_->setEnabled(false);
        messageLabel_->setText(friendlyError(response));
        return;
    }
    currentReport_ = response.body;
    const auto summary = response.body.value(QStringLiteral("summary")).toObject();
    quoteCountLabel_->setText(QString::number(summary.value(QStringLiteral("quoteCount")).toInteger()));
    totalAmountLabel_->setText(money(summary.value(QStringLiteral("totalAmountCents")).toInteger()));
    averageAmountLabel_->setText(money(summary.value(QStringLiteral("averageAmountCents")).toInteger()));
    issuedCountLabel_->setText(QString::number(summary.value(QStringLiteral("issuedCount")).toInteger()));
    voidCountLabel_->setText(QString::number(summary.value(QStringLiteral("voidCount")).toInteger()));
    publishedRateLabel_->setText(percent(summary.value(QStringLiteral("publishedRateBasisPoints")).toInt()));
    fillTable(monthTable_, response.body.value(QStringLiteral("byMonth")).toArray());
    fillTable(customerTable_, response.body.value(QStringLiteral("byCustomer")).toArray());
    fillTable(categoryTable_, response.body.value(QStringLiteral("byMaterialCategory")).toArray());
    messageLabel_->setText(QStringLiteral("统计已更新。所有金额均来自服务端数据库，页面只负责显示。"));
    exportButton_->setEnabled(true);
}

void StatisticsWidget::exportReport() {
    if (currentReport_.isEmpty()) {
        messageLabel_->setText(QStringLiteral("请先查询统计，再导出 Excel。"));
        return;
    }
    auto path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("导出报价统计"),
        QStringLiteral("报价统计.xlsx"),
        QStringLiteral("Excel 工作簿 (*.xlsx)")
    );
    if (path.isEmpty()) {
        return;
    }
    if (!path.endsWith(QStringLiteral(".xlsx"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".xlsx");
    }

    const auto filters = currentReport_.value(QStringLiteral("filters")).toObject();
    const auto summary = currentReport_.value(QStringLiteral("summary")).toObject();
    manage::excel::StatisticsWorkbook workbook;
    workbook.title = QStringLiteral("报价统计（发布率不是成交率）");
    workbook.fromDate = filters.value(QStringLiteral("startDate")).toString();
    workbook.toDate = filters.value(QStringLiteral("endDate")).toString();
    workbook.summary.quoteCount = summary.value(QStringLiteral("quoteCount")).toInteger();
    workbook.summary.totalCents = summary.value(QStringLiteral("totalAmountCents")).toInteger();
    workbook.summary.averageCents = summary.value(QStringLiteral("averageAmountCents")).toInteger();
    workbook.summary.issuedCount = summary.value(QStringLiteral("issuedCount")).toInteger();
    workbook.summary.voidCount = summary.value(QStringLiteral("voidCount")).toInteger();
    workbook.summary.publishedRate =
        static_cast<double>(summary.value(QStringLiteral("publishedRateBasisPoints")).toInt()) /
        10'000.0;
    workbook.monthly = workbookRows(currentReport_.value(QStringLiteral("byMonth")).toArray());
    workbook.customers = workbookRows(currentReport_.value(QStringLiteral("byCustomer")).toArray());
    workbook.categories = workbookRows(
        currentReport_.value(QStringLiteral("byMaterialCategory")).toArray()
    );

    QFile output(path);
    if (!output.open(QIODevice::WriteOnly)) {
        messageLabel_->setText(QStringLiteral("无法保存 Excel：%1").arg(output.errorString()));
        return;
    }
    QString error;
    const auto saved = manage::excel::WorkbookService::exportStatistics(
        &output,
        workbook,
        &error
    );
    messageLabel_->setText(
        saved ? QStringLiteral("统计 Excel 已保存：%1").arg(path)
              : QStringLiteral("导出统计 Excel 失败：%1").arg(error)
    );
}

void StatisticsWidget::setBusy(bool busy) {
    refreshButton_->setEnabled(!busy);
    startDateEdit_->setEnabled(!busy);
    endDateEdit_->setEnabled(!busy);
    customerIdEdit_->setEnabled(!busy);
    statusCombo_->setEnabled(!busy);
    exportButton_->setEnabled(!busy && !currentReport_.isEmpty());
}

} // namespace manage::desktop
