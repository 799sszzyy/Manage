#include "manage/desktop/bom_quote_widget.h"

#include "manage/desktop/api_client.h"
#include "manage/desktop/bom_drag_widgets.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <limits>
#include <optional>

namespace manage::desktop {
namespace {

constexpr auto kIdRole = Qt::UserRole;
constexpr auto kPriceCentsRole = Qt::UserRole + 1;
constexpr auto kCopperBasedRole = Qt::UserRole + 2;

void nameObject(QObject* object, const char* name) {
    object->setObjectName(QString::fromLatin1(name));
}

QString displayMoney(qint64 cents) {
    const auto sign = cents < 0 ? QStringLiteral("-") : QString{};
    const auto absolute = cents < 0 ? -cents : cents;
    return QStringLiteral("%1¥%2.%3")
        .arg(sign)
        .arg(absolute / 100)
        .arg(absolute % 100, 2, 10, QLatin1Char('0'));
}

std::optional<qint64> parseScaledDecimal(
    QString text,
    int decimalPlaces,
    bool requirePositive
) {
    text = text.trimmed();
    if (text.isEmpty() || decimalPlaces < 0 || decimalPlaces > 9) {
        return std::nullopt;
    }

    bool negative = false;
    if (text.startsWith(QLatin1Char('-'))) {
        negative = true;
        text.remove(0, 1);
    } else if (text.startsWith(QLatin1Char('+'))) {
        text.remove(0, 1);
    }
    if (text.isEmpty() || (requirePositive && negative)) {
        return std::nullopt;
    }

    const auto parts = text.split(QLatin1Char('.'));
    if (parts.size() > 2 || parts.first().isEmpty()) {
        return std::nullopt;
    }
    const auto wholeText = parts.first();
    auto fractionalText = parts.size() == 2 ? parts.at(1) : QString{};
    if (fractionalText.size() > decimalPlaces) {
        return std::nullopt;
    }
    for (const auto character : wholeText + fractionalText) {
        if (!character.isDigit()) {
            return std::nullopt;
        }
    }

    while (fractionalText.size() < decimalPlaces) {
        fractionalText.append(QLatin1Char('0'));
    }
    bool wholeOk = false;
    bool fractionalOk = false;
    const auto whole = wholeText.toLongLong(&wholeOk);
    const auto fractional = fractionalText.isEmpty()
                                ? 0
                                : fractionalText.toLongLong(&fractionalOk);
    if (fractionalText.isEmpty()) {
        fractionalOk = true;
    }
    qint64 scale = 1;
    for (int index = 0; index < decimalPlaces; ++index) {
        scale *= 10;
    }
    if (!wholeOk || !fractionalOk ||
        whole > (std::numeric_limits<qint64>::max() - fractional) / scale) {
        return std::nullopt;
    }
    auto result = whole * scale + fractional;
    if (negative) {
        result = -result;
    }
    if (requirePositive && result <= 0) {
        return std::nullopt;
    }
    return result;
}

QTableWidgetItem* readOnlyItem(const QString& text) {
    auto* item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

QString roleName(const QString& role) {
    if (role == QStringLiteral("admin")) {
        return QStringLiteral("管理员");
    }
    if (role == QStringLiteral("quoter")) {
        return QStringLiteral("报价员");
    }
    if (role == QStringLiteral("viewer")) {
        return QStringLiteral("查看员");
    }
    return QStringLiteral("未登录");
}

} // namespace

BomQuoteWidget::BomQuoteWidget(ApiClient* apiClient, QWidget* parent)
    : QWidget(parent), apiClient_(apiClient) {
    setObjectName(QStringLiteral("bomQuoteWidget"));
    buildUi();
    connectUi();
    syncSessionRole();

    if (apiClient_) {
        connect(
            apiClient_,
            &ApiClient::sessionChanged,
            this,
            [this](bool authenticated) {
                Q_UNUSED(authenticated);
                syncSessionRole();
                if (sessionReady()) {
                    refreshBoms();
                    refreshMaterials();
                }
            }
        );
    }
    QTimer::singleShot(0, this, [this]() {
        if (sessionReady()) {
            refreshBoms();
            refreshMaterials();
        }
    });
}

void BomQuoteWidget::buildUi() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    roleLabel_ = new QLabel(this);
    nameObject(roleLabel_, "bomQuoteRoleLabel");
    layout->addWidget(roleLabel_);

    auto* materialSearchLayout = new QHBoxLayout;
    materialSearchEdit_ = new QLineEdit(this);
    nameObject(materialSearchEdit_, "sharedMaterialSearchEdit");
    materialSearchEdit_->setPlaceholderText(
        QStringLiteral("搜索启用物料（编码、名称或规格）")
    );
    materialRefreshButton_ = new QPushButton(
        QStringLiteral("搜索 / 刷新物料"), this
    );
    nameObject(materialRefreshButton_, "sharedMaterialRefreshButton");
    materialSearchLayout->addWidget(new QLabel(QStringLiteral("物料选择范围"), this));
    materialSearchLayout->addWidget(materialSearchEdit_, 1);
    materialSearchLayout->addWidget(materialRefreshButton_);
    layout->addLayout(materialSearchLayout);
    materialSearchStatusLabel_ = new QLabel(
        QStringLiteral("空搜索默认加载前 100 条启用物料；输入关键词可查找更多物料。"),
        this
    );
    nameObject(materialSearchStatusLabel_, "sharedMaterialSearchStatusLabel");
    materialSearchStatusLabel_->setWordWrap(true);
    layout->addWidget(materialSearchStatusLabel_);

    auto* tabs = new QTabWidget(this);
    nameObject(tabs, "bomQuoteTabs");
    tabs->addTab(buildBomPage(), QStringLiteral("BOM 管理"));
    tabs->addTab(buildQuotePage(), QStringLiteral("报价计算"));
    layout->addWidget(tabs, 1);
}

QWidget* BomQuoteWidget::buildBomPage() {
    auto* page = new QWidget(this);
    auto* pageLayout = new QVBoxLayout(page);

    auto* searchLayout = new QHBoxLayout;
    bomSearchEdit_ = new QLineEdit(page);
    nameObject(bomSearchEdit_, "bomSearchEdit");
    bomSearchEdit_->setPlaceholderText(QStringLiteral("按 BOM 编码或名称搜索"));
    bomRefreshButton_ = new QPushButton(QStringLiteral("搜索 / 刷新"), page);
    nameObject(bomRefreshButton_, "bomRefreshButton");
    bomViewButton_ = new QPushButton(QStringLiteral("查看选中项"), page);
    nameObject(bomViewButton_, "bomViewButton");
    bomNewButton_ = new QPushButton(QStringLiteral("新建 BOM"), page);
    nameObject(bomNewButton_, "bomNewButton");
    searchLayout->addWidget(bomSearchEdit_, 1);
    searchLayout->addWidget(bomRefreshButton_);
    searchLayout->addWidget(bomViewButton_);
    searchLayout->addWidget(bomNewButton_);
    pageLayout->addLayout(searchLayout);

    bomListTable_ = new QTableWidget(0, 6, page);
    nameObject(bomListTable_, "bomListTable");
    bomListTable_->setHorizontalHeaderLabels({
        QStringLiteral("ID"), QStringLiteral("编码"), QStringLiteral("名称"),
        QStringLiteral("说明"), QStringLiteral("状态"), QStringLiteral("版本"),
    });
    bomListTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    bomListTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    bomListTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    bomListTable_->horizontalHeader()->setStretchLastSection(true);
    pageLayout->addWidget(bomListTable_, 1);

    auto* editor = new QGroupBox(QStringLiteral("BOM 详情与编辑"), page);
    nameObject(editor, "bomEditorGroup");
    auto* editorLayout = new QVBoxLayout(editor);
    auto* metadata = new QFormLayout;
    bomCodeEdit_ = new QLineEdit(editor);
    nameObject(bomCodeEdit_, "bomCodeEdit");
    bomNameEdit_ = new QLineEdit(editor);
    nameObject(bomNameEdit_, "bomNameEdit");
    bomDescriptionEdit_ = new QLineEdit(editor);
    nameObject(bomDescriptionEdit_, "bomDescriptionEdit");
    bomRevisionLabel_ = new QLabel(QStringLiteral("未保存"), editor);
    nameObject(bomRevisionLabel_, "bomRevisionLabel");
    metadata->addRow(QStringLiteral("编码"), bomCodeEdit_);
    metadata->addRow(QStringLiteral("名称"), bomNameEdit_);
    metadata->addRow(QStringLiteral("说明"), bomDescriptionEdit_);
    metadata->addRow(QStringLiteral("当前版本"), bomRevisionLabel_);
    editorLayout->addLayout(metadata);

    auto* materialLayout = new QHBoxLayout;
    bomMaterialCombo_ = new QComboBox(editor);
    nameObject(bomMaterialCombo_, "bomMaterialCombo");
    bomAddItemButton_ = new QPushButton(QStringLiteral("添加物料明细"), editor);
    nameObject(bomAddItemButton_, "bomAddItemButton");
    bomRemoveItemButton_ = new QPushButton(QStringLiteral("移除选中明细"), editor);
    nameObject(bomRemoveItemButton_, "bomRemoveItemButton");
    materialLayout->addWidget(bomMaterialCombo_, 1);
    materialLayout->addWidget(bomAddItemButton_);
    materialLayout->addWidget(bomRemoveItemButton_);
    editorLayout->addLayout(materialLayout);

    auto* dragLayout = new QHBoxLayout;
    auto* materialDragGroup = new QGroupBox(
        QStringLiteral("物料选择区（拖到右侧）"), editor
    );
    auto* materialDragLayout = new QVBoxLayout(materialDragGroup);
    bomMaterialList_ = new MaterialDragList(materialDragGroup);
    nameObject(bomMaterialList_, "bomMaterialDragList");
    bomMaterialList_->setMinimumWidth(210);
    materialDragLayout->addWidget(bomMaterialList_);

    auto* bomItemsGroup = new QGroupBox(
        QStringLiteral("当前 BOM（拖动行可排序）"), editor
    );
    auto* bomItemsLayout = new QVBoxLayout(bomItemsGroup);
    bomItemsTable_ = new BomItemsTable(bomItemsGroup);
    nameObject(bomItemsTable_, "bomItemsTable");
    bomItemsTable_->horizontalHeader()->setStretchLastSection(true);
    bomItemsLayout->addWidget(bomItemsTable_);
    dragLayout->addWidget(materialDragGroup, 1);
    dragLayout->addWidget(bomItemsGroup, 3);
    editorLayout->addLayout(dragLayout);

    auto* actions = new QHBoxLayout;
    bomSaveButton_ = new QPushButton(QStringLiteral("保存基本信息"), editor);
    nameObject(bomSaveButton_, "bomSaveButton");
    bomReplaceItemsButton_ = new QPushButton(QStringLiteral("替换全部明细"), editor);
    nameObject(bomReplaceItemsButton_, "bomReplaceItemsButton");
    bomToggleEnabledButton_ = new QPushButton(QStringLiteral("停用 BOM"), editor);
    nameObject(bomToggleEnabledButton_, "bomToggleEnabledButton");
    actions->addWidget(bomSaveButton_);
    actions->addWidget(bomReplaceItemsButton_);
    actions->addWidget(bomToggleEnabledButton_);
    actions->addStretch();
    editorLayout->addLayout(actions);
    pageLayout->addWidget(editor, 1);

    bomStatusLabel_ = new QLabel(QStringLiteral("请选择或新建 BOM。"), page);
    nameObject(bomStatusLabel_, "bomStatusLabel");
    bomStatusLabel_->setWordWrap(true);
    pageLayout->addWidget(bomStatusLabel_);
    return page;
}

QWidget* BomQuoteWidget::buildQuotePage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* scope = new QLabel(
        QStringLiteral("当前功能只计算报价，不会保存报价，也不会伪造尚不存在的报价历史接口。"),
        page
    );
    nameObject(scope, "quoteScopeLabel");
    scope->setWordWrap(true);
    layout->addWidget(scope);

    auto* materialLayout = new QHBoxLayout;
    quoteMaterialCombo_ = new QComboBox(page);
    nameObject(quoteMaterialCombo_, "quoteMaterialCombo");
    quoteAddLineButton_ = new QPushButton(QStringLiteral("添加报价物料"), page);
    nameObject(quoteAddLineButton_, "quoteAddLineButton");
    quoteRemoveLineButton_ = new QPushButton(QStringLiteral("移除选中行"), page);
    nameObject(quoteRemoveLineButton_, "quoteRemoveLineButton");
    materialLayout->addWidget(quoteMaterialCombo_, 1);
    materialLayout->addWidget(quoteAddLineButton_);
    materialLayout->addWidget(quoteRemoveLineButton_);
    layout->addLayout(materialLayout);

    quoteLinesTable_ = new QTableWidget(0, 3, page);
    nameObject(quoteLinesTable_, "quoteLinesTable");
    quoteLinesTable_->setHorizontalHeaderLabels({
        QStringLiteral("物料编码"), QStringLiteral("数量"),
        QStringLiteral("单价（元）"),
    });
    quoteLinesTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    quoteLinesTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    quoteLinesTable_->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(quoteLinesTable_, 1);

    auto* inputs = new QFormLayout;
    auto makeMoneySpin = [page](const char* name) {
        auto* spin = new QDoubleSpinBox(page);
        nameObject(spin, name);
        spin->setDecimals(2);
        spin->setRange(0.0, 99'999'999.99);
        spin->setSuffix(QStringLiteral(" 元"));
        return spin;
    };
    freightSpin_ = makeMoneySpin("quoteFreightSpin");
    otherFeesSpin_ = makeMoneySpin("quoteOtherFeesSpin");
    markupSpin_ = new QDoubleSpinBox(page);
    nameObject(markupSpin_, "quoteMarkupSpin");
    markupSpin_->setDecimals(2);
    markupSpin_->setRange(0.0, 100.0);
    markupSpin_->setSuffix(QStringLiteral(" %"));
    taxSpin_ = new QDoubleSpinBox(page);
    nameObject(taxSpin_, "quoteTaxSpin");
    taxSpin_->setDecimals(2);
    taxSpin_->setRange(0.0, 100.0);
    taxSpin_->setSuffix(QStringLiteral(" %"));
    inputs->addRow(QStringLiteral("运费"), freightSpin_);
    inputs->addRow(QStringLiteral("其他费用"), otherFeesSpin_);
    inputs->addRow(QStringLiteral("加价率"), markupSpin_);
    inputs->addRow(QStringLiteral("税率"), taxSpin_);
    layout->addLayout(inputs);

    calculateButton_ = new QPushButton(QStringLiteral("计算报价"), page);
    nameObject(calculateButton_, "quoteCalculateButton");
    layout->addWidget(calculateButton_);

    auto* results = new QFormLayout;
    materialCostLabel_ = new QLabel(QStringLiteral("—"), page);
    nameObject(materialCostLabel_, "quoteMaterialCostLabel");
    priceBeforeTaxLabel_ = new QLabel(QStringLiteral("—"), page);
    nameObject(priceBeforeTaxLabel_, "quotePriceBeforeTaxLabel");
    priceWithTaxLabel_ = new QLabel(QStringLiteral("—"), page);
    nameObject(priceWithTaxLabel_, "quotePriceWithTaxLabel");
    results->addRow(QStringLiteral("材料成本"), materialCostLabel_);
    results->addRow(QStringLiteral("未税价格"), priceBeforeTaxLabel_);
    results->addRow(QStringLiteral("含税价格"), priceWithTaxLabel_);
    layout->addLayout(results);

    quoteStatusLabel_ = new QLabel(QStringLiteral("请添加报价物料。"), page);
    nameObject(quoteStatusLabel_, "quoteStatusLabel");
    quoteStatusLabel_->setWordWrap(true);
    layout->addWidget(quoteStatusLabel_);
    return page;
}

void BomQuoteWidget::connectUi() {
    connect(materialRefreshButton_, &QPushButton::clicked,
            this, &BomQuoteWidget::refreshMaterials);
    connect(materialSearchEdit_, &QLineEdit::returnPressed,
            this, &BomQuoteWidget::refreshMaterials);
    connect(bomRefreshButton_, &QPushButton::clicked,
            this, &BomQuoteWidget::refreshBoms);
    connect(bomSearchEdit_, &QLineEdit::returnPressed,
            this, &BomQuoteWidget::refreshBoms);
    connect(bomViewButton_, &QPushButton::clicked,
            this, &BomQuoteWidget::showSelectedBom);
    connect(bomListTable_, &QTableWidget::cellDoubleClicked,
            this, [this](int, int) { showSelectedBom(); });
    connect(bomListTable_, &QTableWidget::itemSelectionChanged,
            this, &BomQuoteWidget::updateControlState);
    connect(bomNewButton_, &QPushButton::clicked,
            this, &BomQuoteWidget::startNewBom);
    connect(bomSaveButton_, &QPushButton::clicked,
            this, &BomQuoteWidget::saveBom);
    connect(bomReplaceItemsButton_, &QPushButton::clicked,
            this, &BomQuoteWidget::replaceBomItems);
    connect(bomToggleEnabledButton_, &QPushButton::clicked,
            this, &BomQuoteWidget::toggleBomEnabled);
    connect(bomAddItemButton_, &QPushButton::clicked,
            this, &BomQuoteWidget::addBomItem);
    connect(bomRemoveItemButton_, &QPushButton::clicked,
            this, &BomQuoteWidget::removeBomItem);
    connect(bomMaterialList_, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem* item) {
                if (!item || !isAdmin() || pendingRequests_ != 0) {
                    return;
                }
                const auto materialId = item->data(kIdRole).toLongLong();
                const auto index = bomMaterialCombo_->findData(materialId);
                if (index >= 0) {
                    bomMaterialCombo_->setCurrentIndex(index);
                    addBomItem();
                }
            });
    connect(bomItemsTable_, &QTableWidget::itemSelectionChanged,
            this, &BomQuoteWidget::updateControlState);
    // 批次9：新增物料行后异步加载该物料的供应商下拉；铜价档修改后重新解析单价。
    connect(bomItemsTable_, &BomItemsTable::rowAdded, this,
            [this](int row) { loadSuppliersForRow(row, std::nullopt); });
    connect(bomItemsTable_, &QTableWidget::itemChanged, this,
            &BomQuoteWidget::onBomItemsChanged);

    connect(quoteAddLineButton_, &QPushButton::clicked,
            this, &BomQuoteWidget::addQuoteLine);
    connect(quoteRemoveLineButton_, &QPushButton::clicked,
            this, &BomQuoteWidget::removeQuoteLine);
    connect(quoteLinesTable_, &QTableWidget::itemSelectionChanged,
            this, &BomQuoteWidget::updateControlState);
    connect(calculateButton_, &QPushButton::clicked,
            this, &BomQuoteWidget::calculateQuote);
}

void BomQuoteWidget::syncSessionRole() {
    role_.clear();
    mustChangePassword_ = false;
    if (apiClient_ && apiClient_->isAuthenticated()) {
        const auto user = apiClient_->session().user;
        role_ = user.value(QStringLiteral("role")).toString();
        mustChangePassword_ = user.value(QStringLiteral("mustChangePassword")).toBool();
    }
    roleLabel_->setText(
        mustChangePassword_
            ? QStringLiteral("当前身份：%1（请先修改临时密码）").arg(roleName(role_))
            : QStringLiteral("当前身份：%1").arg(roleName(role_))
    );
    updateControlState();
}

void BomQuoteWidget::updateControlState() {
    const auto idle = pendingRequests_ == 0;
    const auto readable = sessionReady();
    const auto writable = isAdmin() && idle;
    const auto calculable = canCalculate() && idle;
    const auto selectedListRow = bomListTable_->currentRow() >= 0;
    const auto selectedBomItem = bomItemsTable_->currentRow() >= 0;
    const auto selectedQuoteLine = quoteLinesTable_->currentRow() >= 0;

    materialSearchEdit_->setEnabled(readable && idle);
    materialRefreshButton_->setEnabled(readable && idle);
    bomSearchEdit_->setEnabled(readable && idle);
    bomRefreshButton_->setEnabled(readable && idle);
    bomViewButton_->setEnabled(readable && idle && selectedListRow);
    bomNewButton_->setEnabled(writable);
    bomCodeEdit_->setReadOnly(!writable);
    bomNameEdit_->setReadOnly(!writable);
    bomDescriptionEdit_->setReadOnly(!writable);
    bomMaterialCombo_->setEnabled(writable && bomMaterialCombo_->count() > 0);
    bomMaterialList_->setEnabled(writable && bomMaterialList_->count() > 0);
    bomItemsTable_->setDragEditingEnabled(writable);
    bomAddItemButton_->setEnabled(writable && bomMaterialCombo_->count() > 0);
    bomRemoveItemButton_->setEnabled(writable && selectedBomItem);
    bomItemsTable_->setEditTriggers(
        writable ? QAbstractItemView::DoubleClicked |
                       QAbstractItemView::EditKeyPressed |
                       QAbstractItemView::SelectedClicked
                 : QAbstractItemView::NoEditTriggers
    );
    bomSaveButton_->setEnabled(writable);
    bomReplaceItemsButton_->setEnabled(writable && currentBomId_ > 0);
    bomToggleEnabledButton_->setEnabled(writable && currentBomId_ > 0);
    bomToggleEnabledButton_->setText(
        currentBomEnabled_ ? QStringLiteral("停用 BOM") : QStringLiteral("启用 BOM")
    );

    quoteMaterialCombo_->setEnabled(calculable && quoteMaterialCombo_->count() > 0);
    quoteAddLineButton_->setEnabled(calculable && quoteMaterialCombo_->count() > 0);
    quoteRemoveLineButton_->setEnabled(calculable && selectedQuoteLine);
    quoteLinesTable_->setEditTriggers(
        calculable ? QAbstractItemView::DoubleClicked |
                         QAbstractItemView::EditKeyPressed |
                         QAbstractItemView::SelectedClicked
                   : QAbstractItemView::NoEditTriggers
    );
    freightSpin_->setEnabled(calculable);
    otherFeesSpin_->setEnabled(calculable);
    markupSpin_->setEnabled(calculable);
    taxSpin_->setEnabled(calculable);
    calculateButton_->setEnabled(calculable && quoteLinesTable_->rowCount() > 0);
}

void BomQuoteWidget::beginRequest(const QString& message, QLabel* statusLabel) {
    ++pendingRequests_;
    statusLabel->setText(message);
    updateControlState();
}

void BomQuoteWidget::endRequest() {
    if (pendingRequests_ > 0) {
        --pendingRequests_;
    }
    updateControlState();
}

void BomQuoteWidget::refreshBoms() {
    if (!apiClient_ || !sessionReady()) {
        bomStatusLabel_->setText(QStringLiteral("请先登录后查看 BOM。"));
        return;
    }
    const auto search = QString::fromUtf8(
        QUrl::toPercentEncoding(bomSearchEdit_->text().trimmed())
    );
    const auto path = QStringLiteral(
        "/api/v1/boms?page=1&pageSize=100&search=%1"
    ).arg(search);
    beginRequest(QStringLiteral("正在加载 BOM 列表…"), bomStatusLabel_);
    QPointer<BomQuoteWidget> self(this);
    apiClient_->get(path, [self](ApiResponse response) {
        if (!self) {
            return;
        }
        self->endRequest();
        if (!response.succeeded()) {
            self->showBomError(response, QStringLiteral("加载 BOM 列表"));
            return;
        }
        const auto items = response.body.value(QStringLiteral("items")).toArray();
        self->bomListTable_->setRowCount(0);
        for (const auto& value : items) {
            const auto object = value.toObject();
            const auto row = self->bomListTable_->rowCount();
            self->bomListTable_->insertRow(row);
            auto* idItem = readOnlyItem(
                QString::number(object.value(QStringLiteral("id")).toInteger())
            );
            idItem->setData(kIdRole, object.value(QStringLiteral("id")).toInteger());
            self->bomListTable_->setItem(row, 0, idItem);
            self->bomListTable_->setItem(row, 1, readOnlyItem(
                object.value(QStringLiteral("code")).toString()
            ));
            self->bomListTable_->setItem(row, 2, readOnlyItem(
                object.value(QStringLiteral("name")).toString()
            ));
            self->bomListTable_->setItem(row, 3, readOnlyItem(
                object.value(QStringLiteral("description")).toString()
            ));
            self->bomListTable_->setItem(row, 4, readOnlyItem(
                object.value(QStringLiteral("isEnabled")).toBool()
                    ? QStringLiteral("启用") : QStringLiteral("停用")
            ));
            self->bomListTable_->setItem(row, 5, readOnlyItem(QString::number(
                object.value(QStringLiteral("revision")).toInt()
            )));
        }
        self->bomStatusLabel_->setText(
            QStringLiteral("已加载 %1 个 BOM。可双击查看详情。").arg(items.size())
        );
    });
}

void BomQuoteWidget::refreshMaterials() {
    if (!apiClient_ || !sessionReady()) {
        return;
    }
    const auto search = QString::fromUtf8(
        QUrl::toPercentEncoding(materialSearchEdit_->text().trimmed())
    );
    const auto path = QStringLiteral(
        "/api/v1/materials?page=1&pageSize=100&enabled=true&search=%1"
    ).arg(search);
    beginRequest(
        QStringLiteral("正在搜索可用物料…"), materialSearchStatusLabel_
    );
    QPointer<BomQuoteWidget> self(this);
    apiClient_->get(
        path,
        [self](ApiResponse response) {
            if (!self) {
                return;
            }
            self->endRequest();
            if (!response.succeeded()) {
                self->materialSearchStatusLabel_->setText(
                    QStringLiteral("搜索物料失败：%1").arg(
                        self->responseError(response)
                    )
                );
                return;
            }
            self->bomMaterialCombo_->clear();
            self->bomMaterialList_->clear();
            self->quoteMaterialCombo_->clear();
            const auto items = response.body.value(QStringLiteral("items")).toArray();
            for (const auto& value : items) {
                const auto material = value.toObject();
                const auto label = QStringLiteral("%1 — %2")
                    .arg(material.value(QStringLiteral("code")).toString(),
                         material.value(QStringLiteral("name")).toString());
                const auto id = material.value(QStringLiteral("id")).toInteger();
                const auto isCopperBased = material
                                               .value(QStringLiteral("isCopperBased"))
                                               .toBool(false);
                self->bomMaterialCombo_->addItem(label, id);
                self->bomMaterialCombo_->setItemData(
                    self->bomMaterialCombo_->count() - 1,
                    isCopperBased,
                    kCopperBasedRole
                );
                self->bomMaterialList_->addMaterial(
                    id,
                    material.value(QStringLiteral("code")).toString(),
                    material.value(QStringLiteral("name")).toString(),
                    isCopperBased
                );
                const auto quoteIndex = self->quoteMaterialCombo_->count();
                self->quoteMaterialCombo_->addItem(label,
                    material.value(QStringLiteral("code")).toString());
                self->quoteMaterialCombo_->setItemData(
                    quoteIndex,
                    material.value(QStringLiteral("currentUnitPriceCents")).toInteger(),
                    kPriceCentsRole
                );
            }
            self->materialSearchStatusLabel_->setText(
                items.isEmpty()
                    ? QStringLiteral("没有找到匹配的启用物料，请更换关键词。")
                    : QStringLiteral("已加载 %1 条匹配物料，两个选择器已同步刷新。")
                          .arg(items.size())
            );
            self->updateControlState();
        }
    );
}

void BomQuoteWidget::showSelectedBom() {
    const auto row = bomListTable_->currentRow();
    if (row < 0 || !bomListTable_->item(row, 0)) {
        bomStatusLabel_->setText(QStringLiteral("请先选择一个 BOM。"));
        return;
    }
    loadBom(bomListTable_->item(row, 0)->data(kIdRole).toLongLong());
}

void BomQuoteWidget::loadBom(qint64 id) {
    if (!apiClient_ || id <= 0) {
        return;
    }
    beginRequest(QStringLiteral("正在加载 BOM 详情…"), bomStatusLabel_);
    QPointer<BomQuoteWidget> self(this);
    apiClient_->get(
        QStringLiteral("/api/v1/boms/%1").arg(id),
        [self](ApiResponse response) {
            if (!self) {
                return;
            }
            self->endRequest();
            if (!response.succeeded()) {
                self->showBomError(response, QStringLiteral("加载 BOM 详情"));
                return;
            }
            self->applyBom(response.body);
            self->bomStatusLabel_->setText(QStringLiteral("BOM 详情已加载。"));
        }
    );
}

void BomQuoteWidget::applyBom(const QJsonObject& object) {
    currentBomId_ = object.value(QStringLiteral("id")).toInteger();
    currentBomRevision_ = object.value(QStringLiteral("revision")).toInt();
    currentBomEnabled_ = object.value(QStringLiteral("isEnabled")).toBool(true);
    bomCodeEdit_->setText(object.value(QStringLiteral("code")).toString());
    bomNameEdit_->setText(object.value(QStringLiteral("name")).toString());
    bomDescriptionEdit_->setText(
        object.value(QStringLiteral("description")).toString()
    );
    bomRevisionLabel_->setText(
        QStringLiteral("%1（ID %2）").arg(currentBomRevision_).arg(currentBomId_)
    );
    ++supplierLoadGeneration_;
    bomItemsTable_->setRowCount(0);
    for (const auto& value : object.value(QStringLiteral("items")).toArray()) {
        const auto item = value.toObject();
        const auto row = bomItemsTable_->rowCount();
        bomItemsTable_->insertRow(row);
        bomItemsTable_->setItem(row, BomItemsTable::Line,
            new QTableWidgetItem(QString::number(
                item.value(QStringLiteral("lineNo")).toInt()
            )));
        auto* materialCode = readOnlyItem(
            item.value(QStringLiteral("materialCode")).toString()
        );
        materialCode->setData(kIdRole,
                              item.value(QStringLiteral("materialId")).toInteger());
        bomItemsTable_->setItem(row, BomItemsTable::Code, materialCode);
        bomItemsTable_->setItem(row, BomItemsTable::Name, readOnlyItem(
            item.value(QStringLiteral("materialName")).toString()
        ));
        auto* supplierCombo = new QComboBox(bomItemsTable_);
        supplierCombo->addItem(QStringLiteral("— 未选择 —"), qint64{0});
        bomItemsTable_->setCellWidget(
            row, BomItemsTable::Supplier, supplierCombo
        );
        const auto copper = item.value(QStringLiteral("copperPriceCents"));
        auto* copperItem = new QTableWidgetItem;
        if (copper.isDouble()) {
            copperItem->setText(QString::number(copper.toInteger() / 100.0, 'f', 2));
        }
        bomItemsTable_->setItem(row, BomItemsTable::Copper, copperItem);
        const auto quantity = item.value(QStringLiteral("quantityMicros")).toInteger();
        bomItemsTable_->setItem(row, BomItemsTable::Quantity,
            new QTableWidgetItem(
                QString::number(static_cast<double>(quantity) / 1'000'000.0, 'f', 6)
            ));
        bomItemsTable_->setItem(row, BomItemsTable::UnitPrice, readOnlyItem(
            QString::number(
                item.value(QStringLiteral("unitPriceCents")).toInteger() / 100.0, 'f', 2
            )
        ));
        bomItemsTable_->setItem(row, BomItemsTable::Notes,
            new QTableWidgetItem(item.value(QStringLiteral("notes")).toString()));
        // 批次9：异步加载该行供应商下拉并回显已保存的供应商与价格快照。
        loadSuppliersForRow(
            row,
            item.value(QStringLiteral("materialSupplierId")).toInteger()
        );
    }
    bomItemsTable_->renumberLines();
    updateControlState();
}

void BomQuoteWidget::startNewBom() {
    currentBomId_ = 0;
    currentBomRevision_ = 0;
    currentBomEnabled_ = true;
    bomCodeEdit_->clear();
    bomNameEdit_->clear();
    bomDescriptionEdit_->clear();
    bomRevisionLabel_->setText(QStringLiteral("未保存"));
    ++supplierLoadGeneration_;
    bomItemsTable_->setRowCount(0);
    bomStatusLabel_->setText(
        QStringLiteral("正在新建 BOM；填写基本信息和明细后点击保存。")
    );
    updateControlState();
}

QJsonObject BomQuoteWidget::bomMetadataPayload() const {
    return QJsonObject{
        {QStringLiteral("code"), bomCodeEdit_->text().trimmed()},
        {QStringLiteral("name"), bomNameEdit_->text().trimmed()},
        {QStringLiteral("description"), bomDescriptionEdit_->text().trimmed()},
    };
}

QJsonArray BomQuoteWidget::bomItemsPayload(bool* ok, QString* error) const {
    *ok = false;
    QJsonArray result;
    for (int row = 0; row < bomItemsTable_->rowCount(); ++row) {
        const auto* lineItem = bomItemsTable_->item(row, BomItemsTable::Line);
        const auto* materialItem = bomItemsTable_->item(row, BomItemsTable::Code);
        const auto* quantityItem = bomItemsTable_->item(row, BomItemsTable::Quantity);
        const auto* notesItem = bomItemsTable_->item(row, BomItemsTable::Notes);
        const auto* copperItem = bomItemsTable_->item(row, BomItemsTable::Copper);
        bool lineOk = false;
        const auto lineNo = lineItem ? lineItem->text().trimmed().toInt(&lineOk) : 0;
        const auto materialId = materialItem
                                    ? materialItem->data(kIdRole).toLongLong()
                                    : 0;
        const auto quantity = quantityItem
                                  ? parseScaledDecimal(quantityItem->text(), 6, true)
                                  : std::nullopt;
        if (!lineOk || lineNo <= 0 || materialId <= 0 || !quantity.has_value()) {
            *error = QStringLiteral(
                "第 %1 行无效：行号、物料和数量必须有效，数量最多 6 位小数。"
            ).arg(row + 1);
            return {};
        }
        QJsonObject item{
            {QStringLiteral("lineNo"), lineNo},
            {QStringLiteral("materialId"), materialId},
            {QStringLiteral("quantityMicros"), *quantity},
            {QStringLiteral("notes"), notesItem ? notesItem->text().trimmed() : QString{}},
        };
        // 批次9：携带供应商与当前铜价（铜价档留空传 null，由服务端解析真实单价）。
        const auto supplierId = bomItemsTable_->rowSupplierId(row);
        item.insert(QStringLiteral("materialSupplierId"), supplierId);
        const auto copperText = copperItem ? copperItem->text().trimmed() : QString{};
        if (copperText.isEmpty()) {
            item.insert(
                QStringLiteral("copperPriceCents"), QJsonValue(QJsonValue::Null)
            );
        } else {
            const auto copper = parseScaledDecimal(copperText, 2, false);
            if (!copper.has_value()) {
                *error = QStringLiteral(
                    "第 %1 行铜价档无效：最多两位小数。"
                ).arg(row + 1);
                return {};
            }
            item.insert(QStringLiteral("copperPriceCents"), *copper);
        }
        result.append(std::move(item));
    }
    *ok = true;
    return result;
}

void BomQuoteWidget::saveBom() {
    if (!apiClient_ || !isAdmin()) {
        return;
    }
    auto payload = bomMetadataPayload();
    if (payload.value(QStringLiteral("code")).toString().isEmpty() ||
        payload.value(QStringLiteral("name")).toString().isEmpty()) {
        bomStatusLabel_->setText(QStringLiteral("BOM 编码和名称不能为空。"));
        return;
    }
    const auto creating = currentBomId_ == 0;
    if (creating) {
        bool itemsOk = false;
        QString itemError;
        const auto items = bomItemsPayload(&itemsOk, &itemError);
        if (!itemsOk) {
            bomStatusLabel_->setText(itemError);
            return;
        }
        payload.insert(QStringLiteral("items"), items);
    } else {
        payload.insert(QStringLiteral("revision"), currentBomRevision_);
    }

    beginRequest(
        creating ? QStringLiteral("正在创建 BOM…")
                 : QStringLiteral("正在保存 BOM 基本信息…"),
        bomStatusLabel_
    );
    QPointer<BomQuoteWidget> self(this);
    auto callback = [self, creating](ApiResponse response) {
        if (!self) {
            return;
        }
        self->endRequest();
        if (!response.succeeded()) {
            self->showBomError(
                response,
                creating ? QStringLiteral("创建 BOM") : QStringLiteral("保存 BOM")
            );
            return;
        }
        self->applyBom(response.body);
        self->bomStatusLabel_->setText(
            creating ? QStringLiteral("BOM 创建成功。")
                     : QStringLiteral("BOM 基本信息已保存；版本号已更新。")
        );
        self->refreshBoms();
    };
    if (creating) {
        apiClient_->post(QStringLiteral("/api/v1/boms"), payload,
                         std::move(callback));
    } else {
        apiClient_->put(
            QStringLiteral("/api/v1/boms/%1").arg(currentBomId_),
            payload,
            std::move(callback)
        );
    }
}

void BomQuoteWidget::replaceBomItems() {
    if (!apiClient_ || !isAdmin() || currentBomId_ <= 0) {
        return;
    }
    bool itemsOk = false;
    QString itemError;
    const auto items = bomItemsPayload(&itemsOk, &itemError);
    if (!itemsOk) {
        bomStatusLabel_->setText(itemError);
        return;
    }
    const QJsonObject payload{
        {QStringLiteral("revision"), currentBomRevision_},
        {QStringLiteral("items"), items},
    };
    beginRequest(QStringLiteral("正在替换 BOM 明细…"), bomStatusLabel_);
    QPointer<BomQuoteWidget> self(this);
    apiClient_->put(
        QStringLiteral("/api/v1/boms/%1/items").arg(currentBomId_),
        payload,
        [self](ApiResponse response) {
            if (!self) {
                return;
            }
            self->endRequest();
            if (!response.succeeded()) {
                self->showBomError(response, QStringLiteral("替换 BOM 明细"));
                return;
            }
            self->applyBom(response.body);
            self->bomStatusLabel_->setText(
                QStringLiteral("BOM 明细已整体替换；版本号已更新。")
            );
            self->refreshBoms();
        }
    );
}

void BomQuoteWidget::toggleBomEnabled() {
    if (!apiClient_ || !isAdmin() || currentBomId_ <= 0) {
        return;
    }
    const auto enabling = !currentBomEnabled_;
    beginRequest(
        enabling ? QStringLiteral("正在启用 BOM…")
                 : QStringLiteral("正在停用 BOM…"),
        bomStatusLabel_
    );
    QPointer<BomQuoteWidget> self(this);
    apiClient_->patch(
        QStringLiteral("/api/v1/boms/%1/enabled").arg(currentBomId_),
        QJsonObject{
            {QStringLiteral("revision"), currentBomRevision_},
            {QStringLiteral("isEnabled"), enabling},
        },
        [self](ApiResponse response) {
            if (!self) {
                return;
            }
            self->endRequest();
            if (!response.succeeded()) {
                self->showBomError(response, QStringLiteral("更改 BOM 状态"));
                return;
            }
            self->applyBom(response.body);
            self->bomStatusLabel_->setText(QStringLiteral("BOM 状态已更新。"));
            self->refreshBoms();
        }
    );
}

void BomQuoteWidget::addBomItem() {
    if (bomMaterialCombo_->currentIndex() < 0) {
        return;
    }
    const auto materialId = bomMaterialCombo_->currentData().toLongLong();
    const auto isCopperBased = bomMaterialCombo_->currentData(kCopperBasedRole)
                                   .toBool(false);
    const auto parts = bomMaterialCombo_->currentText().split(QStringLiteral(" — "));
    if (!bomItemsTable_->addOrMergeMaterial(
            materialId, parts.value(0), parts.value(1), 1'000'000, isCopperBased
        )) {
        bomStatusLabel_->setText(QStringLiteral("物料无效或现有数量无法合并。"));
        return;
    }
    bomStatusLabel_->setText(
        QStringLiteral("物料已加入；若原来已存在，则数量已自动增加。")
    );
    updateControlState();
}

void BomQuoteWidget::removeBomItem() {
    const auto row = bomItemsTable_->currentRow();
    if (row >= 0) {
        bomItemsTable_->removeRow(row);
        bomItemsTable_->renumberLines();
    }
    updateControlState();
}

void BomQuoteWidget::addQuoteLine() {
    if (quoteMaterialCombo_->currentIndex() < 0) {
        return;
    }
    const auto row = quoteLinesTable_->rowCount();
    quoteLinesTable_->insertRow(row);
    quoteLinesTable_->setItem(row, 0, readOnlyItem(
        quoteMaterialCombo_->currentData().toString()
    ));
    quoteLinesTable_->setItem(row, 1, new QTableWidgetItem(QStringLiteral("1.000000")));
    const auto cents = quoteMaterialCombo_->currentData(kPriceCentsRole).toLongLong();
    quoteLinesTable_->setItem(row, 2, new QTableWidgetItem(
        QStringLiteral("%1.%2")
            .arg(cents / 100)
            .arg(cents % 100, 2, 10, QLatin1Char('0'))
    ));
    updateControlState();
}

void BomQuoteWidget::removeQuoteLine() {
    const auto row = quoteLinesTable_->currentRow();
    if (row >= 0) {
        quoteLinesTable_->removeRow(row);
    }
    updateControlState();
}

void BomQuoteWidget::calculateQuote() {
    if (!apiClient_ || !canCalculate()) {
        return;
    }
    QJsonArray lines;
    for (int row = 0; row < quoteLinesTable_->rowCount(); ++row) {
        const auto* code = quoteLinesTable_->item(row, 0);
        const auto* quantityText = quoteLinesTable_->item(row, 1);
        const auto* priceText = quoteLinesTable_->item(row, 2);
        const auto quantity = quantityText
                                  ? parseScaledDecimal(quantityText->text(), 6, true)
                                  : std::nullopt;
        const auto price = priceText
                               ? parseScaledDecimal(priceText->text(), 2, false)
                               : std::nullopt;
        if (!code || code->text().trimmed().isEmpty() || !quantity.has_value() ||
            !price.has_value() || *price < 0) {
            quoteStatusLabel_->setText(QStringLiteral(
                "报价第 %1 行无效：数量最多 6 位小数且必须大于 0，单价最多 2 位小数。"
            ).arg(row + 1));
            return;
        }
        lines.append(QJsonObject{
            {QStringLiteral("materialCode"), code->text().trimmed()},
            {QStringLiteral("quantityMicros"), *quantity},
            {QStringLiteral("unitPriceCents"), *price},
        });
    }
    if (lines.isEmpty()) {
        quoteStatusLabel_->setText(QStringLiteral("请至少添加一行报价物料。"));
        return;
    }

    const auto freight = parseScaledDecimal(
        QString::number(freightSpin_->value(), 'f', 2), 2, false
    );
    const auto otherFees = parseScaledDecimal(
        QString::number(otherFeesSpin_->value(), 'f', 2), 2, false
    );
    const auto markup = parseScaledDecimal(
        QString::number(markupSpin_->value(), 'f', 2), 2, false
    );
    const auto tax = parseScaledDecimal(
        QString::number(taxSpin_->value(), 'f', 2), 2, false
    );
    if (!freight || !otherFees || !markup || !tax) {
        quoteStatusLabel_->setText(QStringLiteral("报价费用或百分比超出允许范围。"));
        return;
    }
    const QJsonObject payload{
        {QStringLiteral("lines"), lines},
        {QStringLiteral("freightCents"), *freight},
        {QStringLiteral("otherFeesCents"), *otherFees},
        {QStringLiteral("markupBasisPoints"), *markup},
        {QStringLiteral("taxBasisPoints"), *tax},
    };

    beginRequest(QStringLiteral("正在计算报价…"), quoteStatusLabel_);
    QPointer<BomQuoteWidget> self(this);
    apiClient_->post(
        QStringLiteral("/api/v1/quotes/calculate"),
        payload,
        [self](ApiResponse response) {
            if (!self) {
                return;
            }
            self->endRequest();
            if (!response.succeeded()) {
                self->showQuoteError(response);
                return;
            }
            self->materialCostLabel_->setText(displayMoney(
                response.body.value(QStringLiteral("materialCostCents")).toInteger()
            ));
            self->priceBeforeTaxLabel_->setText(displayMoney(
                response.body.value(QStringLiteral("priceBeforeTaxCents")).toInteger()
            ));
            self->priceWithTaxLabel_->setText(displayMoney(
                response.body.value(QStringLiteral("priceWithTaxCents")).toInteger()
            ));
            self->quoteStatusLabel_->setText(
                QStringLiteral("报价计算完成；结果未保存到数据库。")
            );
        }
    );
}

void BomQuoteWidget::loadSuppliersForRow(
    int row,
    std::optional<qint64> selectedSupplierId
) {
    if (!apiClient_ || !sessionReady() || row < 0) {
        return;
    }
    const auto materialId = bomItemsTable_->rowMaterialId(row);
    if (materialId <= 0) {
        return;
    }
    const auto generation = supplierLoadGeneration_;
    QPointer<BomQuoteWidget> self(this);
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
                row >= self->bomItemsTable_->rowCount() ||
                self->bomItemsTable_->rowMaterialId(row) != materialId) {
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
            self->bomItemsTable_->setRowSuppliers(
                row, suppliers, selectedSupplierId.value_or(0)
            );
            auto* combo = qobject_cast<QComboBox*>(
                self->bomItemsTable_->cellWidget(
                    row, BomItemsTable::Supplier
                )
            );
            if (combo != nullptr) {
                // 同一行下拉只挂一次监听，供应商变更时重新解析单价。
                QObject::disconnect(combo, nullptr, self, nullptr);
                connect(
                    combo, &QComboBox::currentIndexChanged, self,
                    [self, row](int) { self->onRowSupplierChanged(row); }
                );
            }
            self->resolveRowPrice(row);
        }
    );
}

void BomQuoteWidget::resolveRowPrice(int row) {
    if (!apiClient_ || !sessionReady() || row < 0 ||
        row >= bomItemsTable_->rowCount()) {
        return;
    }
    const auto materialId = bomItemsTable_->rowMaterialId(row);
    const auto supplierId = bomItemsTable_->rowSupplierId(row);
    if (materialId <= 0) {
        return;
    }
    const auto generation = supplierLoadGeneration_;
    auto* copperItem = bomItemsTable_->item(row, BomItemsTable::Copper);
    const auto copperText = copperItem ? copperItem->text().trimmed() : QString{};
    auto path = QStringLiteral(
        "/api/v1/materials/%1/resolve-price?supplierId=%2"
    ).arg(materialId).arg(supplierId);
    if (!copperText.isEmpty()) {
        const auto copper = parseScaledDecimal(copperText, 2, false);
        if (copper.has_value()) {
            path += QStringLiteral("&copperPriceCents=%1").arg(*copper);
        }
    }
    QPointer<BomQuoteWidget> self(this);
    apiClient_->get(
        path,
        [self, row, materialId, generation](ApiResponse response) {
            if (!self) {
                return;
            }
            if (generation != self->supplierLoadGeneration_ ||
                row >= self->bomItemsTable_->rowCount() ||
                self->bomItemsTable_->rowMaterialId(row) != materialId) {
                return;
            }
            auto* priceItem = self->bomItemsTable_->item(
                row, BomItemsTable::UnitPrice
            );
            if (priceItem == nullptr) {
                return;
            }
            if (!response.succeeded()) {
                // 电线类物料已选供应商但尚未填铜价：单价留空待用户补全。
                priceItem->setText({});
                return;
            }
            const auto hasSuppliers = response.body
                                          .value(QStringLiteral("hasSuppliers"))
                                          .toBool(false);
            if (self->bomItemsTable_->rowSupplierId(row) == 0 && hasSuppliers) {
                // 有供应商价格的物料必须先选供应商，才能得到真实价格。
                priceItem->setText({});
                return;
            }
            priceItem->setText(QString::number(
                response.body.value(QStringLiteral("unitPriceCents")).toInteger() / 100.0,
                'f', 2
            ));
        }
    );
}

void BomQuoteWidget::onRowSupplierChanged(int row) {
    resolveRowPrice(row);
}

void BomQuoteWidget::onBomItemsChanged(QTableWidgetItem* item) {
    if (item != nullptr && item->column() == BomItemsTable::Copper) {
        resolveRowPrice(item->row());
    }
}

bool BomQuoteWidget::isAdmin() const {
    return sessionReady() && role_ == QStringLiteral("admin");
}

bool BomQuoteWidget::canCalculate() const {
    return sessionReady() &&
           (role_ == QStringLiteral("admin") || role_ == QStringLiteral("quoter"));
}

bool BomQuoteWidget::sessionReady() const {
    return apiClient_ && apiClient_->isAuthenticated() &&
           !mustChangePassword_ &&
           (role_ == QStringLiteral("admin") ||
            role_ == QStringLiteral("quoter") ||
            role_ == QStringLiteral("viewer"));
}

QString BomQuoteWidget::responseError(const ApiResponse& response) const {
    if (response.error.kind == ApiErrorKind::Network) {
        return QStringLiteral("无法连接本地服务，请确认服务已经启动。");
    }
    const auto code = response.error.code;
    if (code == QStringLiteral("unauthorized") ||
        code == QStringLiteral("session_expired")) {
        return QStringLiteral("登录已失效，请重新登录。");
    }
    if (code == QStringLiteral("forbidden")) {
        return QStringLiteral("当前账号没有执行此操作的权限。");
    }
    if (code == QStringLiteral("password_change_required")) {
        return QStringLiteral("请先修改临时密码，再使用业务功能。");
    }
    if (code == QStringLiteral("revision_conflict")) {
        return QStringLiteral("数据已被其他操作修改，请刷新后重试。");
    }
    if (code == QStringLiteral("duplicate_code")) {
        return QStringLiteral("编码已经存在，请换一个编码。");
    }
    if (code == QStringLiteral("invalid_request") ||
        code == QStringLiteral("invalid_json")) {
        return QStringLiteral("填写的数据不符合要求，请检查后重试。");
    }
    if (code == QStringLiteral("not_found")) {
        return QStringLiteral("目标数据不存在，可能已经被更新。");
    }
    return response.error.message.isEmpty()
               ? QStringLiteral("操作失败，请稍后重试。")
               : QStringLiteral("操作失败：%1").arg(response.error.message);
}

void BomQuoteWidget::showBomError(
    const ApiResponse& response,
    const QString& operation
) {
    bomStatusLabel_->setText(
        QStringLiteral("%1失败：%2").arg(operation, responseError(response))
    );
}

void BomQuoteWidget::showQuoteError(const ApiResponse& response) {
    quoteStatusLabel_->setText(
        QStringLiteral("报价计算失败：%1").arg(responseError(response))
    );
}

} // namespace manage::desktop
