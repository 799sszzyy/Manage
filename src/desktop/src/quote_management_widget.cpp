#include "manage/desktop/quote_management_widget.h"

#include "manage/desktop/api_client.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDate>
#include <QDateEdit>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextEdit>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

#include <cmath>
#include <limits>
#include <optional>

namespace manage::desktop {
namespace {

constexpr int kPageSize = 20;
constexpr int kIdRole = Qt::UserRole;
constexpr int kPriceRole = Qt::UserRole + 1;

template <typename T>
T* named(T* object, const char* name) {
    object->setObjectName(QString::fromLatin1(name));
    return object;
}

QTableWidgetItem* readOnly(const QString& text) {
    auto* item = new QTableWidgetItem(text);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    return item;
}

QString money(qint64 cents) {
    const auto negative = cents < 0;
    const auto absolute = negative ? -cents : cents;
    return QStringLiteral("%1¥%2.%3")
        .arg(negative ? QStringLiteral("-") : QString{})
        .arg(absolute / 100)
        .arg(absolute % 100, 2, 10, QLatin1Char('0'));
}

QString quantity(qint64 micros) {
    auto result = QString::number(static_cast<double>(micros) / 1'000'000.0, 'f', 6);
    while (result.contains(QLatin1Char('.')) && result.endsWith(QLatin1Char('0'))) {
        result.chop(1);
    }
    if (result.endsWith(QLatin1Char('.'))) {
        result.chop(1);
    }
    return result;
}

std::optional<qint64> scaledDecimal(QString text, int places, bool positive) {
    static const QRegularExpression format(QStringLiteral(R"(^\s*(\d+)(?:\.(\d+))?\s*$)"));
    const auto match = format.match(text);
    if (!match.hasMatch() || match.captured(2).size() > places) {
        return std::nullopt;
    }
    bool ok = false;
    const auto whole = match.captured(1).toLongLong(&ok);
    if (!ok) {
        return std::nullopt;
    }
    auto fraction = match.captured(2);
    while (fraction.size() < places) {
        fraction.append(QLatin1Char('0'));
    }
    const auto fractional = fraction.isEmpty() ? 0 : fraction.toLongLong(&ok);
    qint64 scale = 1;
    for (int i = 0; i < places; ++i) {
        scale *= 10;
    }
    if (!ok || whole > (std::numeric_limits<qint64>::max() - fractional) / scale) {
        return std::nullopt;
    }
    const auto result = whole * scale + fractional;
    if (positive && result <= 0) {
        return std::nullopt;
    }
    return result;
}

std::optional<qint64> multiplyQuantity(qint64 quantityMicros, qint64 bomQuantityMicros) {
    if (quantityMicros <= 0 || bomQuantityMicros <= 0) {
        return std::nullopt;
    }
    const auto scaled = static_cast<long double>(quantityMicros) *
                        static_cast<long double>(bomQuantityMicros) / 1'000'000.0L;
    if (!std::isfinite(scaled) ||
        scaled > static_cast<long double>(std::numeric_limits<qint64>::max())) {
        return std::nullopt;
    }
    return static_cast<qint64>(std::llround(scaled));
}

QString stateText(const QString& state) {
    if (state == QStringLiteral("draft")) return QStringLiteral("草稿");
    if (state == QStringLiteral("issued")) return QStringLiteral("已发布");
    if (state == QStringLiteral("void")) return QStringLiteral("已作废");
    return QStringLiteral("未知状态");
}

void selectId(QComboBox* combo, qint64 id, const QString& fallback) {
    const auto index = combo->findData(id, kIdRole);
    if (index >= 0) {
        combo->setCurrentIndex(index);
    } else if (id > 0) {
        combo->addItem(fallback.isEmpty() ? QStringLiteral("ID %1").arg(id) : fallback, id);
        combo->setCurrentIndex(combo->count() - 1);
    }
}

} // namespace

QuoteManagementWidget::QuoteManagementWidget(ApiClient* apiClient, QWidget* parent)
    : QWidget(parent), apiClient_(apiClient) {
    setObjectName(QStringLiteral("quoteManagementWidget"));
    buildUi();
    connectUi();
    applySessionState();
    if (apiClient_) {
        connect(apiClient_, &ApiClient::sessionChanged, this, [this](bool) {
            applySessionState();
        });
    }
    QTimer::singleShot(0, this, [this] {
        if (sessionReady()) {
            loadLookups();
            loadQuotes();
        }
    });
}

void QuoteManagementWidget::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);

    auto* filters = new QHBoxLayout;
    searchEdit_ = named(new QLineEdit(this), "quoteSearchEdit");
    searchEdit_->setPlaceholderText(QStringLiteral("搜索报价编号或客户名称"));
    statusFilter_ = named(new QComboBox(this), "quoteStatusFilter");
    statusFilter_->addItem(QStringLiteral("全部状态"), QString{});
    statusFilter_->addItem(QStringLiteral("草稿"), QStringLiteral("draft"));
    statusFilter_->addItem(QStringLiteral("已发布"), QStringLiteral("issued"));
    statusFilter_->addItem(QStringLiteral("已作废"), QStringLiteral("void"));
    searchButton_ = named(new QPushButton(QStringLiteral("搜索 / 刷新"), this), "quoteSearchButton");
    filters->addWidget(searchEdit_, 1);
    filters->addWidget(statusFilter_);
    filters->addWidget(searchButton_);
    root->addLayout(filters);

    quoteTable_ = named(new QTableWidget(0, 7, this), "quoteListTable");
    quoteTable_->setHorizontalHeaderLabels({
        QStringLiteral("报价编号"), QStringLiteral("客户"), QStringLiteral("状态"),
        QStringLiteral("含税总价"), QStringLiteral("创建时间"), QStringLiteral("修改时间"),
        QStringLiteral("版本"),
    });
    quoteTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    quoteTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    quoteTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    quoteTable_->horizontalHeader()->setStretchLastSection(true);
    root->addWidget(quoteTable_, 1);

    auto* listActions = new QHBoxLayout;
    viewButton_ = named(new QPushButton(QStringLiteral("查看详情"), this), "quoteViewButton");
    newButton_ = named(new QPushButton(QStringLiteral("新建草稿"), this), "quoteNewButton");
    editButton_ = named(new QPushButton(QStringLiteral("编辑草稿"), this), "quoteEditButton");
    issueButton_ = named(new QPushButton(QStringLiteral("发布"), this), "quoteIssueButton");
    voidButton_ = named(new QPushButton(QStringLiteral("作废"), this), "quoteVoidButton");
    cloneButton_ = named(new QPushButton(QStringLiteral("复制为新草稿"), this), "quoteCloneButton");
    deleteButton_ = named(new QPushButton(QStringLiteral("删除草稿"), this), "quoteDeleteButton");
    previousButton_ = named(new QPushButton(QStringLiteral("上一页"), this), "quotePreviousButton");
    nextButton_ = named(new QPushButton(QStringLiteral("下一页"), this), "quoteNextButton");
    pageLabel_ = named(new QLabel(this), "quotePageLabel");
    for (auto* action : {viewButton_, newButton_, editButton_, issueButton_, voidButton_, cloneButton_, deleteButton_}) {
        listActions->addWidget(action);
    }
    listActions->addStretch();
    listActions->addWidget(previousButton_);
    listActions->addWidget(pageLabel_);
    listActions->addWidget(nextButton_);
    root->addLayout(listActions);

    editorGroup_ = named(new QGroupBox(QStringLiteral("报价详情"), this), "quoteEditorGroup");
    auto* editor = new QVBoxLayout(editorGroup_);
    auto* form = new QFormLayout;
    numberLabel_ = named(new QLabel(QStringLiteral("未保存"), editorGroup_), "quoteNumberLabel");
    stateLabel_ = named(new QLabel(QStringLiteral("草稿"), editorGroup_), "quoteStateLabel");
    revisionLabel_ = named(new QLabel(QStringLiteral("-"), editorGroup_), "quoteRevisionLabel");
    customerCombo_ = named(new QComboBox(editorGroup_), "quoteCustomerCombo");
    bomCombo_ = named(new QComboBox(editorGroup_), "quoteBomCombo");
    bomCombo_->addItem(QStringLiteral("不关联 BOM"), qint64{});
    bomQuantitySpin_ = named(new QDoubleSpinBox(editorGroup_), "quoteBomQuantitySpin");
    bomQuantitySpin_->setDecimals(6);
    bomQuantitySpin_->setRange(0.000001, 999'999.999999);
    bomQuantitySpin_->setSingleStep(1.0);
    bomQuantitySpin_->setValue(1.0);
    bomQuantitySpin_->setSuffix(QStringLiteral(" 个"));
    form->addRow(QStringLiteral("报价编号"), numberLabel_);
    form->addRow(QStringLiteral("状态"), stateLabel_);
    form->addRow(QStringLiteral("版本"), revisionLabel_);
    form->addRow(QStringLiteral("客户"), customerCombo_);
    // 工程师责任制：销售指派负责工程师并给出预测的 BOM 构建完成日期。
    engineerCombo_ = named(new QComboBox(editorGroup_), "quoteEngineerCombo");
    engineerCombo_->addItem(QStringLiteral("（未指派）"), qint64{});
    expectedDateEdit_ = named(new QDateEdit(editorGroup_), "quoteExpectedDateEdit");
    expectedDateEdit_->setCalendarPopup(true);
    expectedDateEdit_->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    expectedDateEdit_->setDate(QDate::currentDate().addDays(7));
    form->addRow(QStringLiteral("负责工程师"), engineerCombo_);
    form->addRow(QStringLiteral("预测 BOM 完成日期（销售设置）"), expectedDateEdit_);
    form->addRow(QStringLiteral("关联 BOM（报价基础）"), bomCombo_);
    form->addRow(QStringLiteral("BOM 销售数量"), bomQuantitySpin_);
    bomLeadDaysLabel_ = named(new QLabel(QStringLiteral("-"), editorGroup_), "quoteBomLeadDaysLabel");
    form->addRow(QStringLiteral("BOM 交期（最长物料）"), bomLeadDaysLabel_);
    laborCountSpin_ = named(new QSpinBox(editorGroup_), "quoteLaborCountSpin");
    laborCountSpin_->setRange(1, 9999);
    laborCountSpin_->setValue(1);
    laborCountSpin_->setSuffix(QStringLiteral(" 人"));
    form->addRow(QStringLiteral("产品劳动人数"), laborCountSpin_);
    editor->addLayout(form);

    auto* itemActions = new QHBoxLayout;
    materialSearchEdit_ = named(new QLineEdit(editorGroup_), "quoteSavedMaterialSearchEdit");
    materialSearchEdit_->setPlaceholderText(QStringLiteral("输入编码、名称或规格搜索更多物料"));
    materialSearchButton_ = named(new QPushButton(QStringLiteral("搜索物料"), editorGroup_), "quoteSavedMaterialSearchButton");
    materialCombo_ = named(new QComboBox(editorGroup_), "quoteSavedMaterialCombo");
    addItemButton_ = named(new QPushButton(QStringLiteral("添加物料行"), editorGroup_), "quoteSavedAddItemButton");
    removeItemButton_ = named(new QPushButton(QStringLiteral("移除选中行"), editorGroup_), "quoteSavedRemoveItemButton");
    itemActions->addWidget(materialSearchEdit_, 1);
    itemActions->addWidget(materialSearchButton_);
    itemActions->addWidget(materialCombo_, 1);
    itemActions->addWidget(addItemButton_);
    itemActions->addWidget(removeItemButton_);
    editor->addLayout(itemActions);

    itemsTable_ = named(new QTableWidget(0, 8, editorGroup_), "quoteSavedItemsTable");
    itemsTable_->setHorizontalHeaderLabels({
        QStringLiteral("物料编码"), QStringLiteral("物料名称"), QStringLiteral("规格/单位"),
        QStringLiteral("数量"), QStringLiteral("单价（元）"), QStringLiteral("铜价档（元/吨）"),
        QStringLiteral("供应商"), QStringLiteral("备注"),
    });
    itemsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    itemsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    itemsTable_->horizontalHeader()->setStretchLastSection(true);
    editor->addWidget(itemsTable_, 1);

    auto* processActions = new QHBoxLayout;
    processCombo_ = named(new QComboBox(editorGroup_), "quoteProcessCombo");
    processCombo_->addItem(QStringLiteral("从工序库选择…"), qint64{});
    addProcessButton_ = named(new QPushButton(QStringLiteral("添加工序"), editorGroup_), "quoteAddProcessButton");
    removeProcessButton_ = named(new QPushButton(QStringLiteral("移除选中工序"), editorGroup_), "quoteRemoveProcessButton");
    processActions->addWidget(processCombo_, 1);
    processActions->addWidget(addProcessButton_);
    processActions->addWidget(removeProcessButton_);
    editor->addLayout(processActions);

    processTable_ = named(new QTableWidget(0, 3, editorGroup_), "quoteProcessTable");
    processTable_->setHorizontalHeaderLabels({
        QStringLiteral("行号"), QStringLiteral("工序名称"), QStringLiteral("工时（单人分钟）"),
    });
    processTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    processTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    processTable_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    processTable_->horizontalHeader()->setStretchLastSection(true);
    processTable_->setMaximumHeight(150);
    editor->addWidget(processTable_);

    auto* totals = new QFormLayout;
    auto moneySpin = [this](const char* name) {
        auto* spin = named(new QDoubleSpinBox(editorGroup_), name);
        spin->setDecimals(2);
        spin->setRange(0, 99'999'999.99);
        spin->setSuffix(QStringLiteral(" 元"));
        return spin;
    };
    freightSpin_ = moneySpin("quoteSavedFreightSpin");
    otherFeesSpin_ = moneySpin("quoteSavedOtherFeesSpin");
    markupSpin_ = named(new QDoubleSpinBox(editorGroup_), "quoteSavedMarkupSpin");
    taxSpin_ = named(new QDoubleSpinBox(editorGroup_), "quoteSavedTaxSpin");
    for (auto* spin : {markupSpin_, taxSpin_}) {
        spin->setDecimals(2);
        spin->setRange(0, 100);
        spin->setSuffix(QStringLiteral(" %"));
    }
    notesEdit_ = named(new QTextEdit(editorGroup_), "quoteSavedNotesEdit");
    notesEdit_->setMaximumHeight(70);
    materialCostLabel_ = named(new QLabel(QStringLiteral("-"), editorGroup_), "quoteSavedMaterialCostLabel");
    beforeTaxLabel_ = named(new QLabel(QStringLiteral("-"), editorGroup_), "quoteSavedBeforeTaxLabel");
    withTaxLabel_ = named(new QLabel(QStringLiteral("-"), editorGroup_), "quoteSavedWithTaxLabel");
    totals->addRow(QStringLiteral("运费"), freightSpin_);
    totals->addRow(QStringLiteral("其他费用"), otherFeesSpin_);
    totals->addRow(QStringLiteral("加价率"), markupSpin_);
    totals->addRow(QStringLiteral("税率"), taxSpin_);
    processTotalLabel_ = named(new QLabel(QStringLiteral("-"), editorGroup_), "quoteProcessTotalLabel");
    estimatedDeliveryLabel_ = named(new QLabel(QStringLiteral("-"), editorGroup_), "quoteEstimatedDeliveryLabel");
    totals->addRow(QStringLiteral("工序总工时（单人分钟）"), processTotalLabel_);
    totals->addRow(QStringLiteral("预计发货交期（天）"), estimatedDeliveryLabel_);
    totals->addRow(QStringLiteral("备注"), notesEdit_);
    totals->addRow(QStringLiteral("物料成本（服务端）"), materialCostLabel_);
    totals->addRow(QStringLiteral("税前金额（服务端）"), beforeTaxLabel_);
    totals->addRow(QStringLiteral("含税金额（服务端）"), withTaxLabel_);
    editor->addLayout(totals);

    auto* editorActions = new QHBoxLayout;
    saveButton_ = named(new QPushButton(QStringLiteral("保存草稿"), editorGroup_), "quoteSavedSaveButton");
    cancelButton_ = named(new QPushButton(QStringLiteral("取消编辑"), editorGroup_), "quoteSavedCancelButton");
    editorActions->addWidget(saveButton_);
    editorActions->addWidget(cancelButton_);
    editorActions->addStretch();
    editor->addLayout(editorActions);
    root->addWidget(editorGroup_, 2);

    statusLabel_ = named(new QLabel(QStringLiteral("请登录后查看报价。"), this), "quoteManagementStatusLabel");
    statusLabel_->setWordWrap(true);
    root->addWidget(statusLabel_);
}

void QuoteManagementWidget::connectUi() {
    connect(searchButton_, &QPushButton::clicked, this, &QuoteManagementWidget::refreshQuotes);
    connect(searchEdit_, &QLineEdit::returnPressed, this, &QuoteManagementWidget::refreshQuotes);
    connect(quoteTable_, &QTableWidget::itemSelectionChanged, this, &QuoteManagementWidget::updateControls);
    connect(quoteTable_, &QTableWidget::cellDoubleClicked, this, [this](int, int) { loadSelectedQuote(); });
    connect(viewButton_, &QPushButton::clicked, this, &QuoteManagementWidget::loadSelectedQuote);
    connect(newButton_, &QPushButton::clicked, this, &QuoteManagementWidget::startNewQuote);
    connect(editButton_, &QPushButton::clicked, this, &QuoteManagementWidget::beginEditQuote);
    connect(issueButton_, &QPushButton::clicked, this, [this] { changeStatus(QStringLiteral("issued")); });
    connect(voidButton_, &QPushButton::clicked, this, [this] { changeStatus(QStringLiteral("void")); });
    connect(cloneButton_, &QPushButton::clicked, this, &QuoteManagementWidget::cloneQuote);
    connect(deleteButton_, &QPushButton::clicked, this, &QuoteManagementWidget::deleteDraft);
    connect(previousButton_, &QPushButton::clicked, this, [this] { --page_; loadQuotes(); });
    connect(nextButton_, &QPushButton::clicked, this, [this] { ++page_; loadQuotes(); });
    connect(addItemButton_, &QPushButton::clicked, this, &QuoteManagementWidget::addItem);
    connect(materialSearchButton_, &QPushButton::clicked, this, &QuoteManagementWidget::loadMaterials);
    connect(materialSearchEdit_, &QLineEdit::returnPressed, this, &QuoteManagementWidget::loadMaterials);
    connect(bomCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { loadSelectedBom(); });
    connect(bomQuantitySpin_, &QDoubleSpinBox::editingFinished, this, [this] {
        if (!editing_ || busy_ || loadingBom_ || bomCombo_->currentData(kIdRole).toLongLong() <= 0) {
            return;
        }
        loadSelectedBom(true);
    });
    connect(removeItemButton_, &QPushButton::clicked, this, &QuoteManagementWidget::removeItem);
    // 批次9：铜价档变更后按（供应商, 当前铜价）自动回填真实单价。
    connect(itemsTable_, &QTableWidget::itemChanged, this,
            &QuoteManagementWidget::onItemsChanged);
    connect(processCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) {
                const auto id = processCombo_->currentData(kIdRole).toLongLong();
                addProcessButton_->setEnabled(
                    canWrite() && editing_ && !busy_ && !loadingBom_ && id > 0
                );
            });
    connect(addProcessButton_, &QPushButton::clicked, this, &QuoteManagementWidget::addProcessStep);
    connect(removeProcessButton_, &QPushButton::clicked, this, &QuoteManagementWidget::removeProcessStep);
    const auto refreshEstimate = [this]() {
        qint64 totalMinutes = 0;
        for (int row = 0; row < processTable_->rowCount(); ++row) {
            if (auto* item = processTable_->item(row, 2)) {
                totalMinutes += item->text().toLongLong();
            }
        }
        processTotalLabel_->setText(QString::number(totalMinutes));
        const auto bomLeadDays = bomLeadDaysLabel_->text().toInt();
        const auto labor = qMax(1, laborCountSpin_->value());
        const auto workdayMinutes = 480LL;
        const auto laborDays = totalMinutes <= 0
            ? 0
            : (totalMinutes + labor * workdayMinutes - 1) / (labor * workdayMinutes);
        estimatedDeliveryLabel_->setText(QString::number(bomLeadDays + laborDays));
    };
    connect(processTable_, &QTableWidget::itemChanged, this,
            [refreshEstimate](QTableWidgetItem*) { refreshEstimate(); });
    connect(laborCountSpin_, qOverload<int>(&QSpinBox::valueChanged), this,
            [refreshEstimate](int) { refreshEstimate(); });
    connect(saveButton_, &QPushButton::clicked, this, &QuoteManagementWidget::saveQuote);
    connect(cancelButton_, &QPushButton::clicked, this, [this] { editing_ = false; updateControls(); });
}

bool QuoteManagementWidget::sessionReady() const {
    return apiClient_ && apiClient_->isAuthenticated() && !mustChangePassword_;
}

bool QuoteManagementWidget::canWrite() const {
    return sessionReady() && (role_ == QStringLiteral("admin") || role_ == QStringLiteral("quoter"));
}

void QuoteManagementWidget::applySessionState() {
    role_.clear();
    mustChangePassword_ = false;
    if (apiClient_ && apiClient_->isAuthenticated()) {
        const auto user = apiClient_->session().user;
        role_ = user.value(QStringLiteral("role")).toString();
        mustChangePassword_ = user.value(QStringLiteral("mustChangePassword")).toBool(true);
    }
    if (!sessionReady()) {
        quotes_ = {};
        quoteTable_->setRowCount(0);
        clearEditor();
        statusLabel_->setText(apiClient_ && apiClient_->isAuthenticated()
            ? QStringLiteral("请先修改临时密码，再使用报价管理。")
            : QStringLiteral("请先登录后查看报价。"));
    } else {
        loadLookups();
        loadQuotes();
    }
    updateControls();
}

int QuoteManagementWidget::selectedQuoteRow() const {
    const auto rows = quoteTable_->selectionModel()->selectedRows();
    return rows.isEmpty() || rows.first().row() >= quotes_.size() ? -1 : rows.first().row();
}

void QuoteManagementWidget::updateControls() {
    const auto ready = sessionReady();
    const auto write = canWrite();
    const auto row = selectedQuoteRow();
    const auto selectedStatus = row >= 0
        ? quotes_.at(row).toObject().value(QStringLiteral("status")).toString()
        : QString{};
    searchEdit_->setEnabled(ready && !busy_);
    statusFilter_->setEnabled(ready && !busy_);
    searchButton_->setEnabled(ready && !busy_);
    viewButton_->setEnabled(ready && !busy_ && row >= 0);
    newButton_->setEnabled(write && !busy_);
    editButton_->setEnabled(write && !busy_ && row >= 0 && selectedStatus == QStringLiteral("draft"));
    issueButton_->setEnabled(write && !busy_ && currentId_ > 0 && currentStatus_ == QStringLiteral("draft"));
    voidButton_->setEnabled(write && !busy_ && currentId_ > 0 && currentStatus_ == QStringLiteral("issued"));
    cloneButton_->setEnabled(write && !busy_ && currentId_ > 0);
    deleteButton_->setEnabled(write && !busy_ && currentId_ > 0 && currentStatus_ == QStringLiteral("draft"));
    previousButton_->setEnabled(ready && !busy_ && page_ > 1);
    nextButton_->setEnabled(ready && !busy_ && page_ < totalPages_);

    const auto editable = write && editing_ && currentStatus_ == QStringLiteral("draft") &&
                          !busy_ && !loadingBom_;
    const QList<QWidget*> editorFields{
        customerCombo_, engineerCombo_, expectedDateEdit_, bomCombo_, bomQuantitySpin_, laborCountSpin_, materialSearchEdit_, materialSearchButton_, materialCombo_, addItemButton_, removeItemButton_,
        processCombo_, addProcessButton_, removeProcessButton_,
        freightSpin_, otherFeesSpin_, markupSpin_, taxSpin_, notesEdit_,
    };
    for (auto* field : editorFields) {
        field->setEnabled(editable);
    }
    if (processCombo_->currentData(kIdRole).toLongLong() <= 0) {
        addProcessButton_->setEnabled(false);
    }
    saveButton_->setEnabled(editable);
    cancelButton_->setEnabled(editing_ && !busy_);
    for (int rowIndex = 0; rowIndex < itemsTable_->rowCount(); ++rowIndex) {
        for (int column : {3, 4, 5}) {
            auto* item = itemsTable_->item(rowIndex, column);
            if (item) {
                item->setFlags(editable
                    ? Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable
                    : Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            }
        }
    }
    for (int rowIndex = 0; rowIndex < processTable_->rowCount(); ++rowIndex) {
        for (int column : {1, 2}) {
            auto* item = processTable_->item(rowIndex, column);
            if (item) {
                item->setFlags(editable
                    ? Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable
                    : Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            }
        }
    }
}

void QuoteManagementWidget::refreshQuotes() {
    page_ = 1;
    loadQuotes();
}

void QuoteManagementWidget::loadQuotes() {
    if (!sessionReady() || busy_) return;
    setBusy(true, QStringLiteral("正在加载报价列表…"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("page"), QString::number(page_));
    query.addQueryItem(QStringLiteral("pageSize"), QString::number(kPageSize));
    if (!searchEdit_->text().trimmed().isEmpty()) query.addQueryItem(QStringLiteral("search"), searchEdit_->text().trimmed());
    if (!statusFilter_->currentData().toString().isEmpty()) query.addQueryItem(QStringLiteral("status"), statusFilter_->currentData().toString());
    apiClient_->get(QStringLiteral("/api/v1/quotes?%1").arg(query.toString(QUrl::FullyEncoded)),
        [self = QPointer<QuoteManagementWidget>(this)](ApiResponse response) { if (self) self->showQuotes(response); });
}

void QuoteManagementWidget::showQuotes(const ApiResponse& response) {
    setBusy(false);
    if (!sessionReady()) return;
    if (!response.succeeded()) {
        statusLabel_->setText(errorText(response));
        return;
    }
    const auto values = response.body.value(QStringLiteral("items"));
    if (!values.isArray()) {
        statusLabel_->setText(QStringLiteral("服务器返回的报价列表格式不正确。"));
        return;
    }
    quotes_ = values.toArray();
    quoteTable_->setRowCount(0);
    for (const auto& value : quotes_) {
        const auto quote = value.toObject();
        const auto row = quoteTable_->rowCount();
        quoteTable_->insertRow(row);
        quoteTable_->setItem(row, 0, readOnly(quote.value(QStringLiteral("quoteNumber")).toString()));
        quoteTable_->setItem(row, 1, readOnly(quote.value(QStringLiteral("customerName")).toString()));
        quoteTable_->setItem(row, 2, readOnly(stateText(quote.value(QStringLiteral("status")).toString())));
        quoteTable_->setItem(row, 3, readOnly(money(quote.value(QStringLiteral("priceWithTaxCents")).toInteger())));
        quoteTable_->setItem(row, 4, readOnly(quote.value(QStringLiteral("createdAt")).toString()));
        quoteTable_->setItem(row, 5, readOnly(quote.value(QStringLiteral("updatedAt")).toString()));
        quoteTable_->setItem(row, 6, readOnly(QString::number(quote.value(QStringLiteral("revision")).toInteger())));
    }
    total_ = qMax<qint64>(0, response.body.value(QStringLiteral("total")).toInteger());
    page_ = qMax(1, response.body.value(QStringLiteral("page")).toInt(page_));
    const auto pageSize = qMax(1, response.body.value(QStringLiteral("pageSize")).toInt(kPageSize));
    totalPages_ = qMax(1, static_cast<int>((total_ + pageSize - 1) / pageSize));
    pageLabel_->setText(QStringLiteral("第 %1 / %2 页，共 %3 条").arg(page_).arg(totalPages_).arg(total_));
    statusLabel_->setText(QStringLiteral("已加载 %1 条报价。").arg(quotes_.size()));
    updateControls();
}

void QuoteManagementWidget::loadSelectedQuote() {
    const auto row = selectedQuoteRow();
    if (row < 0 || busy_) return;
    loadQuote(quotes_.at(row).toObject().value(QStringLiteral("id")).toInteger());
}

void QuoteManagementWidget::loadQuote(qint64 id) {
    if (!sessionReady() || id <= 0 || busy_) return;
    setBusy(true, QStringLiteral("正在加载报价详情…"));
    apiClient_->get(QStringLiteral("/api/v1/quotes/%1").arg(id),
        [self = QPointer<QuoteManagementWidget>(this)](ApiResponse response) { if (self) self->showQuoteDetail(response); });
}

void QuoteManagementWidget::showQuoteDetail(const ApiResponse& response) {
    setBusy(false);
    if (!response.succeeded()) {
        statusLabel_->setText(errorText(response));
        return;
    }
    auto quote = response.body.value(QStringLiteral("quote")).toObject();
    if (quote.isEmpty()) quote = response.body;
    if (quote.value(QStringLiteral("id")).toInteger() <= 0) {
        statusLabel_->setText(QStringLiteral("服务器返回的报价详情格式不正确。"));
        return;
    }
    applyDetail(quote);
    editing_ = false;
    statusLabel_->setText(QStringLiteral("报价详情已加载。金额均来自服务端核算结果。"));
    updateControls();
}

void QuoteManagementWidget::loadLookups() {
    if (!sessionReady()) return;
    loadCustomers();
    loadBoms();
    loadMaterials();
    loadProcessSteps();
    loadEngineers();
}

void QuoteManagementWidget::loadEngineers() {
    apiClient_->get(QStringLiteral("/api/v1/users/engineers"),
        [self = QPointer<QuoteManagementWidget>(this)](ApiResponse response) {
            if (!self || !response.succeeded()) return;
            const auto selected = self->engineerCombo_->currentData(kIdRole).toLongLong();
            const QSignalBlocker blocker(self->engineerCombo_);
            self->engineerCombo_->clear();
            self->engineerCombo_->addItem(QStringLiteral("（未指派）"), qint64{});
            for (const auto& value : response.body.value(QStringLiteral("items")).toArray()) {
                const auto user = value.toObject();
                const auto role = user.value(QStringLiteral("role")).toString();
                const auto roleLabel = role == QStringLiteral("admin")
                    ? QStringLiteral("管理员")
                    : role == QStringLiteral("quoter") ? QStringLiteral("报价员")
                                                       : QStringLiteral("只读用户");
                self->engineerCombo_->addItem(
                    QStringLiteral("%1（%2）[%3]").arg(
                        user.value(QStringLiteral("displayName")).toString(),
                        user.value(QStringLiteral("username")).toString(),
                        roleLabel
                    ),
                    user.value(QStringLiteral("id")).toInteger()
                );
            }
            selectId(self->engineerCombo_, selected, {});
        });
}

void QuoteManagementWidget::loadCustomers() {
    apiClient_->get(QStringLiteral("/api/v1/customers?page=1&pageSize=100"),
        [self = QPointer<QuoteManagementWidget>(this)](ApiResponse response) {
            if (!self || !response.succeeded()) return;
            const auto selected = self->customerCombo_->currentData(kIdRole).toLongLong();
            self->customerCombo_->clear();
            for (const auto& value : response.body.value(QStringLiteral("items")).toArray()) {
                const auto customer = value.toObject();
                self->customerCombo_->addItem(customer.value(QStringLiteral("name")).toString(), customer.value(QStringLiteral("id")).toInteger());
            }
            selectId(self->customerCombo_, selected, {});
        });
}

void QuoteManagementWidget::loadBoms() {
    apiClient_->get(QStringLiteral("/api/v1/boms?page=1&pageSize=100&enabled=true"),
        [self = QPointer<QuoteManagementWidget>(this)](ApiResponse response) {
            if (!self || !response.succeeded()) return;
            const auto selected = self->bomCombo_->currentData(kIdRole).toLongLong();
            const QSignalBlocker blocker(self->bomCombo_);
            self->bomCombo_->clear();
            self->bomCombo_->addItem(QStringLiteral("不关联 BOM"), qint64{});
            for (const auto& value : response.body.value(QStringLiteral("items")).toArray()) {
                const auto bom = value.toObject();
                self->bomCombo_->addItem(QStringLiteral("%1 - %2").arg(bom.value(QStringLiteral("code")).toString(), bom.value(QStringLiteral("name")).toString()), bom.value(QStringLiteral("id")).toInteger());
            }
            selectId(self->bomCombo_, selected, {});
            self->loadedBomId_ = self->bomCombo_->currentData(kIdRole).toLongLong();
        });
}

void QuoteManagementWidget::loadMaterials() {
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("page"), QStringLiteral("1"));
    query.addQueryItem(QStringLiteral("pageSize"), QStringLiteral("100"));
    query.addQueryItem(QStringLiteral("enabled"), QStringLiteral("true"));
    const auto search = materialSearchEdit_->text().trimmed();
    if (!search.isEmpty()) query.addQueryItem(QStringLiteral("search"), search);
    apiClient_->get(QStringLiteral("/api/v1/materials?%1").arg(query.toString(QUrl::FullyEncoded)),
        [self = QPointer<QuoteManagementWidget>(this)](ApiResponse response) {
            if (!self || !response.succeeded()) return;
            self->materialCombo_->clear();
            for (const auto& value : response.body.value(QStringLiteral("items")).toArray()) {
                const auto material = value.toObject();
                self->materialCombo_->addItem(QStringLiteral("%1 - %2").arg(material.value(QStringLiteral("code")).toString(), material.value(QStringLiteral("name")).toString()), material.value(QStringLiteral("id")).toInteger());
                const auto index = self->materialCombo_->count() - 1;
                self->materialCombo_->setItemData(index, material.value(QStringLiteral("currentUnitPriceCents")).toInteger(), kPriceRole);
                self->materialCombo_->setItemData(index, material, Qt::UserRole + 2);
            }
            self->updateControls();
        });
}

void QuoteManagementWidget::loadProcessSteps() {
    if (!sessionReady()) return;
    apiClient_->get(QStringLiteral("/api/v1/process-steps?page=1&pageSize=100&enabled=true"),
        [self = QPointer<QuoteManagementWidget>(this)](ApiResponse response) {
            if (!self || !response.succeeded()) return;
            self->processLibrary_ = response.body.value(QStringLiteral("items")).toArray();
            const QSignalBlocker blocker(self->processCombo_);
            self->processCombo_->clear();
            self->processCombo_->addItem(QStringLiteral("从工序库选择…"), qint64{});
            for (const auto& value : self->processLibrary_) {
                const auto step = value.toObject();
                const auto index = self->processCombo_->count();
                self->processCombo_->addItem(
                    QStringLiteral("%1 - %2（%3 分钟/人）")
                        .arg(step.value(QStringLiteral("code")).toString(),
                             step.value(QStringLiteral("name")).toString())
                        .arg(step.value(QStringLiteral("laborMinutes")).toInteger()),
                    step.value(QStringLiteral("id")).toInteger()
                );
                self->processCombo_->setItemData(index, step, Qt::UserRole + 2);
            }
        });
}

void QuoteManagementWidget::addProcessStep() {
    if (!canWrite() || !editing_ || busy_ || loadingBom_) return;
    const auto id = processCombo_->currentData(kIdRole).toLongLong();
    if (id <= 0) {
        statusLabel_->setText(QStringLiteral("请先在工序库中选择一道工序。"));
        return;
    }
    const auto step = processCombo_->currentData(Qt::UserRole + 2).value<QJsonObject>();
    if (step.isEmpty()) return;
    const auto row = processTable_->rowCount();
    processTable_->insertRow(row);
    processTable_->setItem(row, 0, readOnly(QString::number(row + 1)));
    processTable_->setItem(row, 1, new QTableWidgetItem(step.value(QStringLiteral("name")).toString()));
    processTable_->setItem(row, 2, new QTableWidgetItem(QString::number(step.value(QStringLiteral("laborMinutes")).toInteger())));
    statusLabel_->setText(QStringLiteral("已添加工序：%1。可继续调整工时或直接修改名称。")
        .arg(step.value(QStringLiteral("name")).toString()));
    updateControls();
}

void QuoteManagementWidget::removeProcessStep() {
    if (!canWrite() || !editing_ || busy_) return;
    const auto rows = processTable_->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
        statusLabel_->setText(QStringLiteral("请先选择要移除的工序行。"));
        return;
    }
    processTable_->removeRow(rows.first().row());
    for (int row = 0; row < processTable_->rowCount(); ++row) {
        if (auto* item = processTable_->item(row, 0)) {
            item->setText(QString::number(row + 1));
        }
    }
    updateControls();
}

void QuoteManagementWidget::loadSelectedBom(bool forceReload) {
    if (!editing_ || !sessionReady() || busy_ || loadingBom_) {
        return;
    }

    const auto selectedId = bomCombo_->currentData(kIdRole).toLongLong();
    const auto selectedQuantityMicros = qRound64(bomQuantitySpin_->value() * 1'000'000.0);
    if (!forceReload && selectedId == loadedBomId_ &&
        selectedQuantityMicros == loadedBomQuantityMicros_) {
        return;
    }
    const auto previousId = loadedBomId_;
    const auto previousQuantityMicros = loadedBomQuantityMicros_;
    if (selectedId > 0 && itemsTable_->rowCount() > 0) {
        const auto answer = QMessageBox::question(
            this,
            QStringLiteral("重新加载 BOM 明细"),
            QStringLiteral("更换 BOM 或修改销售数量会重新展开 BOM，并替换当前报价明细；之后仍可继续添加单独物料。是否继续？"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No
        );
        if (answer != QMessageBox::Yes) {
            const QSignalBlocker blocker(bomCombo_);
            selectId(bomCombo_, previousId, {});
            bomQuantitySpin_->setValue(static_cast<double>(previousQuantityMicros) / 1'000'000.0);
            return;
        }
    }

    ++bomLoadGeneration_;
    pendingBom_ = {};
    pendingBomItems_ = {};
    pendingBomMaterials_ = {};
    pendingBomIndex_ = 0;
    loadedBomId_ = selectedId;
    loadedBomQuantityMicros_ = selectedQuantityMicros;
    if (selectedId <= 0) {
        statusLabel_->setText(QStringLiteral("已取消 BOM 关联；报价草稿必须关联一个 BOM。"));
        updateControls();
        return;
    }

    const auto generation = bomLoadGeneration_;
    loadingBom_ = true;
    setBusy(true, QStringLiteral("正在读取 BOM 明细并补齐物料价格……"));
    apiClient_->get(
        QStringLiteral("/api/v1/boms/%1").arg(selectedId),
        [self = QPointer<QuoteManagementWidget>(this), generation](ApiResponse response) {
            if (!self || generation != self->bomLoadGeneration_) return;
            if (!response.succeeded()) {
                self->loadingBom_ = false;
                self->setBusy(false);
                self->statusLabel_->setText(self->errorText(response));
                self->updateControls();
                return;
            }
            self->pendingBom_ = response.body;
            self->pendingBomItems_ = response.body.value(QStringLiteral("items")).toArray();
            if (self->pendingBomItems_.isEmpty()) {
                self->loadingBom_ = false;
                self->setBusy(false);
                self->statusLabel_->setText(QStringLiteral("所选 BOM 没有明细，无法建立报价草稿。"));
                self->updateControls();
                return;
            }
            self->pendingBomIndex_ = 0;
            self->loadNextBomMaterial();
        }
    );
}

void QuoteManagementWidget::loadNextBomMaterial() {
    if (!loadingBom_) return;
    if (pendingBomIndex_ >= pendingBomItems_.size()) {
        itemsTable_->setRowCount(0);
        for (const auto& value : pendingBomMaterials_) {
            const auto material = value.toObject();
            // 批次9：BOM 条目携带供应商与解析单价，展开为报价行时一并带入。
            std::optional<qint64> supplierId;
            const auto supplierValue = material.value(QStringLiteral("_bomSupplierId"));
            if (supplierValue.isDouble() && supplierValue.toInteger() > 0) {
                supplierId = supplierValue.toInteger();
            }
            std::optional<qint64> copperCents;
            const auto copperValue = material.value(QStringLiteral("_bomCopperCents"));
            if (copperValue.isDouble()) {
                copperCents = copperValue.toInteger();
            }
            const auto unitPriceCents = material
                                            .value(QStringLiteral("_bomUnitPriceCents"))
                                            .toInteger();
            addMaterialRow(
                material,
                material.value(QStringLiteral("_bomQuantityMicros")).toInteger(),
                material.value(QStringLiteral("_bomNotes")).toString(),
                supplierId,
                copperCents,
                unitPriceCents
            );
        }
        loadingBom_ = false;
        setBusy(false);
        const auto bomName = pendingBom_.value(QStringLiteral("name")).toString();
        statusLabel_->setText(
            QStringLiteral("已从 BOM“%1”载入 %2 条物料；可继续添加单独物料。")
                .arg(bomName)
                .arg(pendingBomMaterials_.size())
        );
        updateControls();
        return;
    }

    const auto generation = bomLoadGeneration_;
    const auto bomItem = pendingBomItems_.at(pendingBomIndex_).toObject();
    const auto materialId = bomItem.value(QStringLiteral("materialId")).toInteger();
    if (materialId <= 0) {
        loadingBom_ = false;
        setBusy(false);
        statusLabel_->setText(QStringLiteral("BOM 中存在无效物料，无法建立报价草稿。"));
        updateControls();
        return;
    }
    apiClient_->get(
        QStringLiteral("/api/v1/materials/%1").arg(materialId),
        [self = QPointer<QuoteManagementWidget>(this), generation, bomItem,
            bomQuantityMicros = loadedBomQuantityMicros_](ApiResponse response) {
            if (!self || generation != self->bomLoadGeneration_) return;
            if (!response.succeeded()) {
                self->loadingBom_ = false;
                self->setBusy(false);
                self->statusLabel_->setText(self->errorText(response));
                self->updateControls();
                return;
            }
            auto material = response.body;
            const auto expandedQuantity = multiplyQuantity(
                bomItem.value(QStringLiteral("quantityMicros")).toInteger(),
                bomQuantityMicros
            );
            if (!expandedQuantity.has_value()) {
                self->loadingBom_ = false;
                self->setBusy(false);
                self->statusLabel_->setText(QStringLiteral("BOM 销售数量过大，无法展开物料数量。"));
                self->updateControls();
                return;
            }
            material.insert(QStringLiteral("_bomQuantityMicros"),
                            *expandedQuantity);
            material.insert(QStringLiteral("_bomNotes"),
                            bomItem.value(QStringLiteral("notes")).toString());
            // 批次9：BOM 条目的供应商、铜价档与解析单价随物料带入报价行。
            material.insert(
                QStringLiteral("_bomSupplierId"),
                bomItem.value(QStringLiteral("materialSupplierId")).toInteger()
            );
            material.insert(
                QStringLiteral("_bomCopperCents"),
                bomItem.value(QStringLiteral("copperPriceCents"))
            );
            material.insert(
                QStringLiteral("_bomUnitPriceCents"),
                bomItem.value(QStringLiteral("unitPriceCents")).toInteger()
            );
            self->pendingBomMaterials_.append(material);
            ++self->pendingBomIndex_;
            self->loadNextBomMaterial();
        }
    );
}

void QuoteManagementWidget::startNewQuote() {
    if (!canWrite()) return;
    clearEditor();
    currentStatus_ = QStringLiteral("draft");
    stateLabel_->setText(QStringLiteral("草稿（尚未保存）"));
    editing_ = true;
    editorGroup_->setTitle(QStringLiteral("新建报价草稿"));
    updateControls();
}

void QuoteManagementWidget::beginEditQuote() {
    const auto row = selectedQuoteRow();
    if (!canWrite() || row < 0 || quotes_.at(row).toObject().value(QStringLiteral("status")).toString() != QStringLiteral("draft")) return;
    const auto id = quotes_.at(row).toObject().value(QStringLiteral("id")).toInteger();
    setBusy(true, QStringLiteral("正在加载草稿以便编辑…"));
    apiClient_->get(QStringLiteral("/api/v1/quotes/%1").arg(id),
        [self = QPointer<QuoteManagementWidget>(this)](ApiResponse response) {
            if (!self) return;
            self->setBusy(false);
            if (!response.succeeded()) { self->statusLabel_->setText(self->errorText(response)); return; }
            auto quote = response.body.value(QStringLiteral("quote")).toObject();
            if (quote.isEmpty()) quote = response.body;
            self->applyDetail(quote);
            self->editing_ = true;
            self->editorGroup_->setTitle(QStringLiteral("编辑报价草稿"));
            self->statusLabel_->setText(QStringLiteral("可以修改草稿；保存后金额由服务端重新核算。"));
            self->updateControls();
        });
}

void QuoteManagementWidget::addItem() {
    if (!editing_ || materialCombo_->currentIndex() < 0) return;
    const auto material = materialCombo_->currentData(Qt::UserRole + 2).toJsonObject();
    addMaterialRow(material, 1'000'000, {});
    updateControls();
}

void QuoteManagementWidget::addMaterialRow(
    const QJsonObject& material,
    qint64 quantityMicros,
    const QString& notes,
    std::optional<qint64> supplierId,
    std::optional<qint64> copperPriceCents,
    qint64 unitPriceCents
) {
    const auto row = itemsTable_->rowCount();
    const auto materialId = material.value(QStringLiteral("id")).toInteger();
    itemsTable_->insertRow(row);
    auto* code = readOnly(material.value(QStringLiteral("code")).toString());
    code->setData(kIdRole, materialId);
    itemsTable_->setItem(row, 0, code);
    itemsTable_->setItem(row, 1, readOnly(material.value(QStringLiteral("name")).toString()));
    itemsTable_->setItem(row, 2, readOnly(QStringLiteral("%1 / %2").arg(material.value(QStringLiteral("specification")).toString(), material.value(QStringLiteral("unit")).toString())));
    itemsTable_->setItem(row, 3, new QTableWidgetItem(quantity(quantityMicros)));
    // 单价：BOM 带入的解析单价优先，否则回退物料默认单价（用户可再手动修改）。
    const auto cents = unitPriceCents > 0
                           ? unitPriceCents
                           : material.value(QStringLiteral("currentUnitPriceCents")).toInteger();
    itemsTable_->setItem(row, 4, new QTableWidgetItem(QString::number(cents / 100.0, 'f', 2)));
    auto* copperItem = new QTableWidgetItem;
    if (copperPriceCents.has_value()) {
        copperItem->setText(QString::number(*copperPriceCents / 100.0, 'f', 2));
    }
    itemsTable_->setItem(row, 5, copperItem);
    // 第 6 列：供应商下拉（由 loadRowSuppliers 异步填充并回显）。
    auto* supplierCombo = new QComboBox(itemsTable_);
    supplierCombo->addItem(QStringLiteral("— 未选择 —"), qint64{0});
    itemsTable_->setCellWidget(row, 6, supplierCombo);
    itemsTable_->setItem(row, 7, new QTableWidgetItem(notes));
    if (materialId > 0) {
        loadRowSuppliers(row, materialId, supplierId);
    }
}

void QuoteManagementWidget::loadRowSuppliers(
    int row,
    qint64 materialId,
    std::optional<qint64> selectedSupplierId
) {
    if (!apiClient_ || materialId <= 0 || row < 0) {
        return;
    }
    const auto generation = supplierLoadGeneration_;
    QPointer<QuoteManagementWidget> self(this);
    apiClient_->get(
        QStringLiteral("/api/v1/materials/%1/suppliers?page=1&pageSize=100")
            .arg(materialId),
        [self, row, materialId, generation, selectedSupplierId](
            ApiResponse response
        ) {
            if (!self) {
                return;
            }
            if (generation != self->supplierLoadGeneration_ ||
                row >= self->itemsTable_->rowCount()) {
                return;
            }
            const auto* codeItem = self->itemsTable_->item(row, 0);
            if (codeItem == nullptr ||
                codeItem->data(kIdRole).toLongLong() != materialId) {
                return;
            }
            std::vector<std::pair<qint64, QString>> suppliers;
            if (response.succeeded()) {
                const auto items = response.body.value(QStringLiteral("items")).toArray();
                for (const auto& value : items) {
                    const auto supplier = value.toObject();
                    suppliers.emplace_back(
                        supplier.value(QStringLiteral("id")).toInteger(),
                        supplier.value(QStringLiteral("supplierName")).toString()
                    );
                }
            }
            auto* combo = qobject_cast<QComboBox*>(
                self->itemsTable_->cellWidget(row, 6)
            );
            if (combo == nullptr) {
                return;
            }
            const QSignalBlocker blocker(combo);
            combo->clear();
            combo->addItem(QStringLiteral("— 未选择 —"), qint64{0});
            for (const auto& [id, name] : suppliers) {
                combo->addItem(name, id);
            }
            const auto index = combo->findData(selectedSupplierId.value_or(0));
            combo->setCurrentIndex(index >= 0 ? index : 0);
            QObject::disconnect(combo, nullptr, self, nullptr);
            connect(
                combo, &QComboBox::currentIndexChanged, self,
                [self, row](int) { self->onRowSupplierChanged(row); }
            );
            // 新行单价为空时自动解析回填；已保存报价行保留历史快照。
            const auto* priceItem = self->itemsTable_->item(row, 4);
            if (priceItem == nullptr || priceItem->text().trimmed().isEmpty()) {
                self->resolveRowPrice(row);
            }
        }
    );
}

void QuoteManagementWidget::resolveRowPrice(int row) {
    if (!apiClient_ || row < 0 || row >= itemsTable_->rowCount()) {
        return;
    }
    const auto* codeItem = itemsTable_->item(row, 0);
    if (codeItem == nullptr) {
        return;
    }
    const auto materialId = codeItem->data(kIdRole).toLongLong();
    auto* combo = qobject_cast<QComboBox*>(itemsTable_->cellWidget(row, 6));
    const auto supplierId = combo == nullptr ? 0 : combo->currentData().toLongLong();
    if (materialId <= 0) {
        return;
    }
    const auto generation = supplierLoadGeneration_;
    auto* copperItem = itemsTable_->item(row, 5);
    const auto copperText = copperItem ? copperItem->text().trimmed() : QString{};
    auto path = QStringLiteral(
        "/api/v1/materials/%1/resolve-price?supplierId=%2"
    ).arg(materialId).arg(supplierId);
    if (!copperText.isEmpty()) {
        const auto copper = scaledDecimal(copperText, 2, false);
        if (copper.has_value()) {
            path += QStringLiteral("&copperPriceCents=%1").arg(*copper);
        }
    }
    QPointer<QuoteManagementWidget> self(this);
    apiClient_->get(
        path,
        [self, row, materialId, generation](ApiResponse response) {
            if (!self) {
                return;
            }
            if (generation != self->supplierLoadGeneration_ ||
                row >= self->itemsTable_->rowCount()) {
                return;
            }
            const auto* codeItem = self->itemsTable_->item(row, 0);
            if (codeItem == nullptr ||
                codeItem->data(kIdRole).toLongLong() != materialId) {
                return;
            }
            auto* priceItem = self->itemsTable_->item(row, 4);
            if (priceItem == nullptr) {
                return;
            }
            if (!response.succeeded()) {
                // 解析失败（如电线类尚未填铜价）：保留当前显示值，
                // 保存时由服务端做最终校验并给出明确提示。
                return;
            }
            const auto hasSuppliers = response.body
                                          .value(QStringLiteral("hasSuppliers"))
                                          .toBool(false);
            auto* combo = qobject_cast<QComboBox*>(
                self->itemsTable_->cellWidget(row, 6)
            );
            const auto supplierId = combo == nullptr ? 0 : combo->currentData().toLongLong();
            if (supplierId == 0 && hasSuppliers) {
                // 有供应商价格的物料尚未选择供应商：保留当前显示值。
                return;
            }
            priceItem->setText(QString::number(
                response.body.value(QStringLiteral("unitPriceCents")).toInteger() / 100.0,
                'f', 2
            ));
        }
    );
}

void QuoteManagementWidget::onRowSupplierChanged(int row) {
    resolveRowPrice(row);
}

void QuoteManagementWidget::onItemsChanged(QTableWidgetItem* item) {
    if (item != nullptr && item->column() == 5) {
        resolveRowPrice(item->row());
    }
}

void QuoteManagementWidget::removeItem() {
    const auto rows = itemsTable_->selectionModel()->selectedRows();
    if (editing_ && !rows.isEmpty()) itemsTable_->removeRow(rows.first().row());
}

QJsonObject QuoteManagementWidget::editorPayload(bool includeRevision, bool* ok) const {
    *ok = false;
    const auto customerId = customerCombo_->currentData(kIdRole).toLongLong();
    const auto bomId = bomCombo_->currentData(kIdRole).toLongLong();
    const auto bomQuantityMicros = qRound64(bomQuantitySpin_->value() * 1'000'000.0);
    if (customerId <= 0 || bomId <= 0 || bomQuantityMicros <= 0 || itemsTable_->rowCount() == 0) return {};
    QJsonArray items;
    for (int row = 0; row < itemsTable_->rowCount(); ++row) {
        const auto quantityValue = scaledDecimal(itemsTable_->item(row, 3)->text(), 6, true);
        const auto priceValue = scaledDecimal(itemsTable_->item(row, 4)->text(), 2, false);
        const auto materialId = itemsTable_->item(row, 0)->data(kIdRole).toLongLong();
        if (!quantityValue || !priceValue || materialId <= 0) return {};
        QJsonObject itemBody{
            {QStringLiteral("materialId"), materialId},
            {QStringLiteral("quantityMicros"), *quantityValue},
            {QStringLiteral("unitPriceCents"), *priceValue},
            {QStringLiteral("notes"), itemsTable_->item(row, 7)->text().trimmed()},
        };
        // 批次9：报价行携带供应商（0 = 未指定）。
        auto* supplierCombo = qobject_cast<QComboBox*>(
            itemsTable_->cellWidget(row, 6)
        );
        itemBody.insert(
            QStringLiteral("materialSupplierId"),
            supplierCombo == nullptr ? 0 : supplierCombo->currentData().toLongLong()
        );
        // 铜价档（元/吨）：留空表示普通物料，传 null；填写则解析为分。
        const auto copperText = itemsTable_->item(row, 5)->text().trimmed();
        if (copperText.isEmpty()) {
            itemBody.insert(
                QStringLiteral("copperPriceCents"), QJsonValue(QJsonValue::Null)
            );
        } else {
            const auto copperValue = scaledDecimal(copperText, 2, false);
            if (!copperValue) return {};
            itemBody.insert(QStringLiteral("copperPriceCents"), *copperValue);
        }
        items.append(std::move(itemBody));
    }
    QJsonArray processSteps;
    for (int row = 0; row < processTable_->rowCount(); ++row) {
        const auto stepName = processTable_->item(row, 1)->text().trimmed();
        const auto minutes = processTable_->item(row, 2)->text().toLongLong();
        if (stepName.isEmpty() || minutes < 0) return {};
        processSteps.append(QJsonObject{
            {QStringLiteral("stepName"), stepName},
            {QStringLiteral("laborMinutes"), minutes},
        });
    }
    const auto engineerId = engineerCombo_->currentData(kIdRole).toLongLong();
    QJsonObject body{
        {QStringLiteral("customerId"), customerId},
        {QStringLiteral("freightCents"), qRound64(freightSpin_->value() * 100.0)},
        {QStringLiteral("otherFeesCents"), qRound64(otherFeesSpin_->value() * 100.0)},
        {QStringLiteral("markupBasisPoints"), qRound64(markupSpin_->value() * 100.0)},
        {QStringLiteral("taxBasisPoints"), qRound64(taxSpin_->value() * 100.0)},
        {QStringLiteral("notes"), notesEdit_->toPlainText().trimmed()},
        {QStringLiteral("items"), items},
        {QStringLiteral("bomQuantityMicros"), bomQuantityMicros},
        {QStringLiteral("laborCount"), laborCountSpin_->value()},
        {QStringLiteral("processSteps"), processSteps},
        // 工程师责任制：指派工程师 + 预测 BOM 构建完成时间（未指派时传 null）。
        {QStringLiteral("engineerId"), engineerId > 0
             ? QJsonValue(engineerId)
             : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("expectedCompletionAt"),
             QDateTime(expectedDateEdit_->date(), QTime(0, 0)).toString(Qt::ISODateWithMs)},
    };
    body.insert(QStringLiteral("bomTemplateId"), bomId);
    if (includeRevision) body.insert(QStringLiteral("revision"), currentRevision_);
    *ok = true;
    return body;
}

void QuoteManagementWidget::saveQuote() {
    if (!canWrite() || !editing_ || busy_) return;
    bool ok = false;
    const auto updating = currentId_ > 0;
    const auto body = editorPayload(updating, &ok);
    if (!ok) {
        statusLabel_->setText(QStringLiteral("请先选择客户、关联 BOM 并确认物料明细；数量须大于 0，单价最多保留两位小数。"));
        return;
    }
    setBusy(true, updating ? QStringLiteral("正在保存草稿修改…") : QStringLiteral("正在创建报价草稿…"));
    const auto callback = [self = QPointer<QuoteManagementWidget>(this), updating](ApiResponse response) {
        if (!self) return;
        self->setBusy(false);
        if (!response.succeeded()) { self->statusLabel_->setText(self->errorText(response)); return; }
        auto quote = response.body.value(QStringLiteral("quote")).toObject();
        if (quote.isEmpty()) quote = response.body;
        self->applyDetail(quote);
        self->editing_ = false;
        self->statusLabel_->setText(updating ? QStringLiteral("报价草稿修改成功，金额已由服务端重新核算。") : QStringLiteral("报价草稿创建成功，金额已由服务端核算。"));
        self->updateControls();
        self->loadQuotes();
    };
    if (updating) apiClient_->put(QStringLiteral("/api/v1/quotes/%1").arg(currentId_), body, callback);
    else apiClient_->post(QStringLiteral("/api/v1/quotes"), body, callback);
}

void QuoteManagementWidget::changeStatus(const QString& status) {
    if (!canWrite() || currentId_ <= 0 || busy_) return;
    setBusy(true, status == QStringLiteral("issued") ? QStringLiteral("正在发布报价…") : QStringLiteral("正在作废报价…"));
    apiClient_->patch(QStringLiteral("/api/v1/quotes/%1/status").arg(currentId_),
        QJsonObject{{QStringLiteral("status"), status}, {QStringLiteral("revision"), currentRevision_}},
        [self = QPointer<QuoteManagementWidget>(this), status](ApiResponse response) {
            if (!self) return;
            self->setBusy(false);
            if (!response.succeeded()) { self->statusLabel_->setText(self->errorText(response)); return; }
            auto quote = response.body.value(QStringLiteral("quote")).toObject();
            if (quote.isEmpty()) quote = response.body;
            self->applyDetail(quote);
            self->editing_ = false;
            self->statusLabel_->setText(status == QStringLiteral("issued") ? QStringLiteral("报价已发布，内容已冻结。") : QStringLiteral("报价已作废，仍可查询和复制。"));
            self->loadQuotes();
        });
}

void QuoteManagementWidget::cloneQuote() {
    if (!canWrite() || currentId_ <= 0 || busy_) return;
    setBusy(true, QStringLiteral("正在复制报价…"));
    apiClient_->post(QStringLiteral("/api/v1/quotes/%1/clone").arg(currentId_), {},
        [self = QPointer<QuoteManagementWidget>(this)](ApiResponse response) {
            if (!self) return;
            self->setBusy(false);
            if (!response.succeeded()) { self->statusLabel_->setText(self->errorText(response)); return; }
            auto quote = response.body.value(QStringLiteral("quote")).toObject();
            if (quote.isEmpty()) quote = response.body;
            self->applyDetail(quote);
            self->editing_ = true;
            self->editorGroup_->setTitle(QStringLiteral("复制生成的新草稿"));
            self->statusLabel_->setText(QStringLiteral("已复制为新的报价草稿，可继续修改。"));
            self->loadQuotes();
        });
}

void QuoteManagementWidget::deleteDraft() {
    if (!canWrite() || currentId_ <= 0 || currentStatus_ != QStringLiteral("draft") || busy_) return;
    if (QMessageBox::question(this, QStringLiteral("确认删除草稿"),
            QStringLiteral("确定删除报价草稿 %1 吗？此操作无法撤销。").arg(numberLabel_->text()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    setBusy(true, QStringLiteral("正在删除报价草稿…"));
    apiClient_->remove(QStringLiteral("/api/v1/quotes/%1?revision=%2").arg(currentId_).arg(currentRevision_),
        [self = QPointer<QuoteManagementWidget>(this)](ApiResponse response) {
            if (!self) return;
            self->setBusy(false);
            if (!response.succeeded()) { self->statusLabel_->setText(self->errorText(response)); return; }
            self->clearEditor();
            self->statusLabel_->setText(QStringLiteral("报价草稿已删除。"));
            self->loadQuotes();
        });
}

void QuoteManagementWidget::applyDetail(const QJsonObject& quote) {
    currentId_ = quote.value(QStringLiteral("id")).toInteger();
    currentRevision_ = quote.value(QStringLiteral("revision")).toInteger();
    currentStatus_ = quote.value(QStringLiteral("status")).toString();
    numberLabel_->setText(quote.value(QStringLiteral("quoteNumber")).toString());
    stateLabel_->setText(stateText(currentStatus_));
    revisionLabel_->setText(QString::number(currentRevision_));
    selectId(customerCombo_, quote.value(QStringLiteral("customerId")).toInteger(), quote.value(QStringLiteral("customerName")).toString());
    {
        const QSignalBlocker blocker(engineerCombo_);
        selectId(engineerCombo_, quote.value(QStringLiteral("assignedEngineerId")).toInteger(), {});
    }
    {
        const auto expectedText = quote.value(QStringLiteral("expectedCompletionAt")).toString();
        const auto expected = QDateTime::fromString(expectedText, Qt::ISODateWithMs);
        if (expected.isValid()) {
            expectedDateEdit_->setDate(expected.toLocalTime().date());
        }
    }
    {
        const QSignalBlocker blocker(bomCombo_);
        selectId(bomCombo_, quote.value(QStringLiteral("bomTemplateId")).toInteger(), quote.value(QStringLiteral("bomName")).toString());
        loadedBomId_ = bomCombo_->currentData(kIdRole).toLongLong();
    }
    loadedBomQuantityMicros_ = quote.value(QStringLiteral("bomQuantityMicros")).toInteger();
    if (loadedBomQuantityMicros_ <= 0) loadedBomQuantityMicros_ = 1'000'000;
    bomQuantitySpin_->setValue(static_cast<double>(loadedBomQuantityMicros_) / 1'000'000.0);
    bomLeadDaysLabel_->setText(QString::number(quote.value(QStringLiteral("bomLeadDays")).toInt()));
    laborCountSpin_->setValue(qMax(1, quote.value(QStringLiteral("laborCount")).toInt(1)));
    processTotalLabel_->setText(QString::number(quote.value(QStringLiteral("processTotalMinutes")).toInteger()));
    estimatedDeliveryLabel_->setText(QString::number(quote.value(QStringLiteral("estimatedDeliveryDays")).toInt()));
    freightSpin_->setValue(quote.value(QStringLiteral("freightCents")).toInteger() / 100.0);
    otherFeesSpin_->setValue(quote.value(QStringLiteral("otherFeesCents")).toInteger() / 100.0);
    markupSpin_->setValue(quote.value(QStringLiteral("markupBasisPoints")).toInteger() / 100.0);
    taxSpin_->setValue(quote.value(QStringLiteral("taxBasisPoints")).toInteger() / 100.0);
    notesEdit_->setPlainText(quote.value(QStringLiteral("notes")).toString());
    materialCostLabel_->setText(money(quote.value(QStringLiteral("materialCostCents")).toInteger()));
    beforeTaxLabel_->setText(money(quote.value(QStringLiteral("priceBeforeTaxCents")).toInteger()));
    withTaxLabel_->setText(money(quote.value(QStringLiteral("priceWithTaxCents")).toInteger()));
    ++supplierLoadGeneration_;
    {
        // 回显期间屏蔽表格信号，避免铜价列 setItem 触发解析覆盖历史快照。
        const QSignalBlocker blocker(itemsTable_);
        itemsTable_->setRowCount(0);
    for (const auto& value : quote.value(QStringLiteral("items")).toArray()) {
        const auto item = value.toObject();
        const auto row = itemsTable_->rowCount();
        itemsTable_->insertRow(row);
        auto* code = readOnly(item.value(QStringLiteral("materialCode")).toString());
        code->setData(kIdRole, item.value(QStringLiteral("materialId")).toInteger());
        itemsTable_->setItem(row, 0, code);
        itemsTable_->setItem(row, 1, readOnly(item.value(QStringLiteral("materialName")).toString()));
        itemsTable_->setItem(row, 2, readOnly(QStringLiteral("%1 / %2").arg(item.value(QStringLiteral("specification")).toString(), item.value(QStringLiteral("unit")).toString())));
        itemsTable_->setItem(row, 3, new QTableWidgetItem(quantity(item.value(QStringLiteral("quantityMicros")).toInteger())));
        itemsTable_->setItem(row, 4, new QTableWidgetItem(QString::number(item.value(QStringLiteral("unitPriceCents")).toInteger() / 100.0, 'f', 2)));
        // 铜价档：nullable 整数（元/吨，精确到分）；null 显示空串。
        const auto copperValue = item.value(QStringLiteral("copperPriceCents"));
        auto* copperItem = new QTableWidgetItem;
        if (copperValue.isDouble()) {
            copperItem->setText(QString::number(copperValue.toInteger() / 100.0, 'f', 2));
        }
        itemsTable_->setItem(row, 5, copperItem);
        // 第 6 列：供应商（先以快照名称占位，再异步加载完整下拉回显）。
        auto* supplierCombo = new QComboBox(itemsTable_);
        supplierCombo->addItem(QStringLiteral("— 未选择 —"), qint64{0});
        const auto supplierId = item.value(QStringLiteral("materialSupplierId")).toInteger();
        if (supplierId > 0) {
            const auto supplierName = item.value(QStringLiteral("supplierName")).toString();
            supplierCombo->addItem(
                supplierName.isEmpty() ? QStringLiteral("供应商 %1").arg(supplierId) : supplierName,
                supplierId
            );
            supplierCombo->setCurrentIndex(1);
        }
        itemsTable_->setCellWidget(row, 6, supplierCombo);
        itemsTable_->setItem(row, 7, new QTableWidgetItem(item.value(QStringLiteral("notes")).toString()));
        // 异步刷新供应商下拉（保留已保存的选择）。
        if (item.value(QStringLiteral("materialId")).toInteger() > 0) {
            loadRowSuppliers(
                row,
                item.value(QStringLiteral("materialId")).toInteger(),
                supplierId > 0 ? std::optional<qint64>(supplierId) : std::nullopt
            );
        }
    }
    }
    processTable_->setRowCount(0);
    for (const auto& value : quote.value(QStringLiteral("processSteps")).toArray()) {
        const auto step = value.toObject();
        const auto row = processTable_->rowCount();
        processTable_->insertRow(row);
        processTable_->setItem(row, 0, readOnly(QString::number(row + 1)));
        processTable_->setItem(row, 1, new QTableWidgetItem(step.value(QStringLiteral("stepName")).toString()));
        processTable_->setItem(row, 2, new QTableWidgetItem(QString::number(step.value(QStringLiteral("laborMinutes")).toInteger())));
    }
    editorGroup_->setTitle(QStringLiteral("报价详情"));
    updateControls();
}

void QuoteManagementWidget::clearEditor() {
    currentId_ = 0;
    currentRevision_ = 0;
    loadedBomId_ = 0;
    loadedBomQuantityMicros_ = 1'000'000;
    currentStatus_.clear();
    editing_ = false;
    numberLabel_->setText(QStringLiteral("未保存"));
    stateLabel_->setText(QStringLiteral("-"));
    revisionLabel_->setText(QStringLiteral("-"));
    if (customerCombo_->count()) customerCombo_->setCurrentIndex(0);
    {
        const QSignalBlocker blocker(engineerCombo_);
        if (engineerCombo_->count()) engineerCombo_->setCurrentIndex(0);
    }
    expectedDateEdit_->setDate(QDate::currentDate().addDays(7));
    {
        const QSignalBlocker blocker(bomCombo_);
        if (bomCombo_->count()) bomCombo_->setCurrentIndex(0);
    }
    bomQuantitySpin_->setValue(1.0);
    bomLeadDaysLabel_->setText(QStringLiteral("-"));
    laborCountSpin_->setValue(1);
    processTotalLabel_->setText(QStringLiteral("-"));
    estimatedDeliveryLabel_->setText(QStringLiteral("-"));
    pendingBom_ = {};
    pendingBomItems_ = {};
    pendingBomMaterials_ = {};
    pendingBomIndex_ = 0;
    loadingBom_ = false;
    ++supplierLoadGeneration_;
    itemsTable_->setRowCount(0);
    processTable_->setRowCount(0);
    freightSpin_->setValue(0);
    otherFeesSpin_->setValue(0);
    markupSpin_->setValue(0);
    taxSpin_->setValue(0);
    notesEdit_->clear();
    materialCostLabel_->setText(QStringLiteral("-"));
    beforeTaxLabel_->setText(QStringLiteral("-"));
    withTaxLabel_->setText(QStringLiteral("-"));
    editorGroup_->setTitle(QStringLiteral("报价详情"));
}

QString QuoteManagementWidget::errorText(const ApiResponse& response) const {
    if (response.error.kind == ApiErrorKind::Network) return QStringLiteral("网络连接失败，请确认本地服务已启动。%1").arg(response.error.message.isEmpty() ? QString{} : QStringLiteral("（%1）").arg(response.error.message));
    if (response.httpStatus == 401) return QStringLiteral("登录已失效，请重新登录。");
    if (response.httpStatus == 403) return QStringLiteral("当前账号没有报价操作权限。");
    if (response.httpStatus == 409 || response.error.code == QStringLiteral("revision_conflict")) return QStringLiteral("操作冲突：报价可能已被修改或状态不允许，请刷新后重试。");
    if (response.httpStatus == 404) return QStringLiteral("报价不存在或已被删除，请刷新列表。");
    if (response.httpStatus == 400) return QStringLiteral("提交内容不符合要求，请检查客户、物料、数量和金额。");
    return QStringLiteral("操作失败（%1）：%2").arg(response.error.code.isEmpty() ? QStringLiteral("unknown") : response.error.code, response.error.message.isEmpty() ? QStringLiteral("未知错误") : response.error.message);
}

void QuoteManagementWidget::setBusy(bool busy, const QString& message) {
    busy_ = busy;
    if (!message.isEmpty()) statusLabel_->setText(message);
    updateControls();
}

} // namespace manage::desktop
