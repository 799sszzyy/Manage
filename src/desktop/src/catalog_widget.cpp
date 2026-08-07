#include "manage/desktop/catalog_widget.h"

#include "manage/desktop/api_client.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSplitter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTabWidget>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

#include <limits>
#include <optional>
#include <utility>

namespace manage::desktop {
namespace {

constexpr int kPageSize = 20;
constexpr qint64 kMaxSafeJsonInteger = 9'007'199'254'740'991;

QPushButton* button(const QString& text, const QString& objectName, QWidget* parent) {
    auto* result = new QPushButton(text, parent);
    result->setObjectName(objectName);
    return result;
}

QLineEdit* lineEdit(const QString& objectName, QWidget* parent) {
    auto* result = new QLineEdit(parent);
    result->setObjectName(objectName);
    return result;
}

QTableWidgetItem* readOnlyItem(const QString& text) {
    auto* item = new QTableWidgetItem(text);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    return item;
}

QString priceText(qint64 cents) {
    const auto whole = cents / 100;
    const auto fraction = qAbs(cents % 100);
    return QStringLiteral("%1.%2")
        .arg(whole)
        .arg(fraction, 2, 10, QLatin1Char('0'));
}

std::optional<qint64> parsePrice(const QString& text) {
    static const QRegularExpression expression(
        QStringLiteral(R"(^\s*(\d+)(?:\.(\d{1,2}))?\s*$)")
    );
    const auto match = expression.match(text);
    if (!match.hasMatch()) {
        return std::nullopt;
    }

    bool ok = false;
    const auto whole = match.captured(1).toLongLong(&ok);
    if (!ok) {
        return std::nullopt;
    }
    auto fractionText = match.captured(2);
    if (fractionText.size() == 1) {
        fractionText.append(QLatin1Char('0'));
    }
    const auto fraction = fractionText.isEmpty() ? 0 : fractionText.toInt(&ok);
    if (!ok || whole > (kMaxSafeJsonInteger - fraction) / 100) {
        return std::nullopt;
    }
    return whole * 100 + fraction;
}

} // namespace

CatalogWidget::CatalogWidget(ApiClient* apiClient, QWidget* parent)
    : QWidget(parent), apiClient_(apiClient) {
    setObjectName(QStringLiteral("catalogWidget"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("catalogTabs"));
    tabs->addTab(createMaterialsPage(), QStringLiteral("物料库"));
    tabs->addTab(createCustomersPage(), QStringLiteral("客户库"));
    layout->addWidget(tabs);

    connectActions();
    updateWriteAccess();
    if (apiClient_) {
        connect(
            apiClient_,
            &ApiClient::sessionChanged,
            this,
            [this](bool) { applySessionState(); }
        );
        applySessionState();
    } else {
        materialsStatusLabel_->setText(QStringLiteral("API 客户端不可用。"));
        customersStatusLabel_->setText(QStringLiteral("API 客户端不可用。"));
    }
}

QWidget* CatalogWidget::createMaterialsPage() {
    auto* page = new QWidget(this);
    page->setObjectName(QStringLiteral("materialsPage"));
    auto* pageLayout = new QVBoxLayout(page);

    auto* searchLayout = new QHBoxLayout;
    materialsSearchEdit_ = lineEdit(QStringLiteral("materialsSearchEdit"), page);
    materialsSearchEdit_->setPlaceholderText(QStringLiteral("按编码、名称或规格搜索"));
    materialsSearchButton_ = button(
        QStringLiteral("搜索"), QStringLiteral("materialsSearchButton"), page
    );
    materialsRefreshButton_ = button(
        QStringLiteral("刷新"), QStringLiteral("materialsRefreshButton"), page
    );
    // 常用操作按钮放在页面顶部，与搜索栏同一行，便于快速访问。
    materialAddButton_ = button(
        QStringLiteral("新增物料"), QStringLiteral("materialAddButton"), page
    );
    materialEditButton_ = button(
        QStringLiteral("编辑物料"), QStringLiteral("materialEditButton"), page
    );
    materialToggleButton_ = button(
        QStringLiteral("停用物料"), QStringLiteral("materialToggleButton"), page
    );
    searchLayout->addWidget(materialsSearchEdit_, 1);
    searchLayout->addWidget(materialsSearchButton_);
    searchLayout->addWidget(materialsRefreshButton_);
    searchLayout->addStretch();
    searchLayout->addWidget(materialAddButton_);
    searchLayout->addWidget(materialEditButton_);
    searchLayout->addWidget(materialToggleButton_);
    pageLayout->addLayout(searchLayout);

    auto* splitter = new QSplitter(Qt::Horizontal, page);
    splitter->setObjectName(QStringLiteral("materialsSplitter"));
    materialsTable_ = new QTableWidget(splitter);
    materialsTable_->setObjectName(QStringLiteral("materialsTable"));
    materialsTable_->setColumnCount(9);
    materialsTable_->setHorizontalHeaderLabels({
        QStringLiteral("编码"),
        QStringLiteral("名称"),
        QStringLiteral("规格"),
        QStringLiteral("单位"),
        QStringLiteral("分类"),
        QStringLiteral("电线类"),
        QStringLiteral("单价（元）"),
        QStringLiteral("状态"),
        QStringLiteral("版本"),
    });
    materialsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    materialsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    materialsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    materialsTable_->verticalHeader()->setVisible(false);
    materialsTable_->horizontalHeader()->setStretchLastSection(true);
    splitter->addWidget(materialsTable_);
    splitter->addWidget(createMaterialEditor());
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    pageLayout->addWidget(splitter, 1);

    auto* actions = new QHBoxLayout;
    materialsPreviousButton_ = button(
        QStringLiteral("上一页"), QStringLiteral("materialsPreviousButton"), page
    );
    materialsNextButton_ = button(
        QStringLiteral("下一页"), QStringLiteral("materialsNextButton"), page
    );
    materialsPageLabel_ = new QLabel(page);
    materialsPageLabel_->setObjectName(QStringLiteral("materialsPageLabel"));
    actions->addStretch();
    actions->addWidget(materialsPreviousButton_);
    actions->addWidget(materialsPageLabel_);
    actions->addWidget(materialsNextButton_);
    pageLayout->addLayout(actions);

    materialsStatusLabel_ = new QLabel(page);
    materialsStatusLabel_->setObjectName(QStringLiteral("materialsStatusLabel"));
    materialsStatusLabel_->setWordWrap(true);
    pageLayout->addWidget(materialsStatusLabel_);
    return page;
}

QWidget* CatalogWidget::createMaterialEditor() {
    materialEditor_ = new QGroupBox(QStringLiteral("物料信息"), this);
    materialEditor_->setObjectName(QStringLiteral("materialEditor"));
    auto* outer = new QVBoxLayout(materialEditor_);

    // 物料基本信息（三级向导第 1 步）
    materialBaseGroup_ = new QGroupBox(QStringLiteral("第 1 步 · 基本信息"), materialEditor_);
    materialBaseGroup_->setObjectName(QStringLiteral("materialBaseGroup"));
    auto* layout = new QFormLayout(materialBaseGroup_);
    materialCodeEdit_ = lineEdit(QStringLiteral("materialCodeEdit"), materialBaseGroup_);
    materialNameEdit_ = lineEdit(QStringLiteral("materialNameEdit"), materialBaseGroup_);
    materialSpecificationEdit_ = lineEdit(
        QStringLiteral("materialSpecificationEdit"), materialBaseGroup_
    );
    materialUnitEdit_ = lineEdit(QStringLiteral("materialUnitEdit"), materialBaseGroup_);
    materialCategoryEdit_ = lineEdit(
        QStringLiteral("materialCategoryEdit"), materialBaseGroup_
    );
    materialCopperCheck_ = new QCheckBox(QStringLiteral("电线类（按铜价定价）"), materialBaseGroup_);
    materialCopperCheck_->setObjectName(QStringLiteral("materialCopperCheck"));
    materialPriceEdit_ = lineEdit(QStringLiteral("materialPriceEdit"), materialBaseGroup_);
    materialPriceEdit_->setPlaceholderText(QStringLiteral("例如 12.50"));
    materialEnabledCheck_ = new QCheckBox(QStringLiteral("启用"), materialBaseGroup_);
    materialEnabledCheck_->setObjectName(QStringLiteral("materialEnabledCheck"));
    materialSaveButton_ = button(
        QStringLiteral("保存"), QStringLiteral("materialSaveButton"), materialBaseGroup_
    );
    materialCancelButton_ = button(
        QStringLiteral("取消"), QStringLiteral("materialCancelButton"), materialBaseGroup_
    );

    layout->addRow(QStringLiteral("编码 *"), materialCodeEdit_);
    layout->addRow(QStringLiteral("名称 *"), materialNameEdit_);
    layout->addRow(QStringLiteral("规格"), materialSpecificationEdit_);
    layout->addRow(QStringLiteral("单位 *"), materialUnitEdit_);
    layout->addRow(QStringLiteral("分类"), materialCategoryEdit_);
    layout->addRow(QString(), materialCopperCheck_);
    layout->addRow(QStringLiteral("单价（元）*"), materialPriceEdit_);
    layout->addRow(QString(), materialEnabledCheck_);
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(materialSaveButton_);
    buttons->addWidget(materialCancelButton_);
    layout->addRow(buttons);
    outer->addWidget(materialBaseGroup_);

    // 供应商分支（三级向导第 2 步）
    supplierGroupBox_ = new QGroupBox(QStringLiteral("第 2 步 · 供应商分支"), materialEditor_);
    supplierGroupBox_->setObjectName(QStringLiteral("supplierGroupBox"));
    auto* supplierLayout = new QVBoxLayout(supplierGroupBox_);
    suppliersTable_ = new QTableWidget(supplierGroupBox_);
    suppliersTable_->setObjectName(QStringLiteral("suppliersTable"));
    suppliersTable_->setColumnCount(7);
    suppliersTable_->setHorizontalHeaderLabels({
        QStringLiteral("供应商"),
        QStringLiteral("联系人"),
        QStringLiteral("电话"),
        QStringLiteral("供货周期（天）"),
        QStringLiteral("默认"),
        QStringLiteral("状态"),
        QStringLiteral("版本"),
    });
    suppliersTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    suppliersTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    suppliersTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    suppliersTable_->verticalHeader()->setVisible(false);
    suppliersTable_->horizontalHeader()->setStretchLastSection(true);
    supplierLayout->addWidget(suppliersTable_);

    auto* supplierActions = new QHBoxLayout;
    supplierAddButton_ = button(
        QStringLiteral("新建供应商"), QStringLiteral("supplierAddButton"),
        supplierGroupBox_
    );
    supplierEditButton_ = button(
        QStringLiteral("编辑选中"), QStringLiteral("supplierEditButton"),
        supplierGroupBox_
    );
    supplierToggleButton_ = button(
        QStringLiteral("停用选中"), QStringLiteral("supplierToggleButton"),
        supplierGroupBox_
    );
    // 向导第 2 步完成：确认供应商并进入价格设置（编辑模式下隐藏）。
    supplierConfirmButton_ = button(
        QStringLiteral("确定供应商 →"), QStringLiteral("supplierConfirmButton"),
        supplierGroupBox_
    );
    supplierConfirmButton_->setVisible(false);
    supplierActions->addWidget(supplierAddButton_);
    supplierActions->addWidget(supplierEditButton_);
    supplierActions->addWidget(supplierToggleButton_);
    supplierActions->addStretch();
    supplierActions->addWidget(supplierConfirmButton_);
    supplierLayout->addLayout(supplierActions);

    suppliersStatusLabel_ = new QLabel(supplierGroupBox_);
    suppliersStatusLabel_->setObjectName(QStringLiteral("suppliersStatusLabel"));
    suppliersStatusLabel_->setWordWrap(true);
    supplierLayout->addWidget(suppliersStatusLabel_);

    supplierEditor_ = new QGroupBox(QStringLiteral("供应商信息"), supplierGroupBox_);
    supplierEditor_->setObjectName(QStringLiteral("supplierEditor"));
    auto* supplierForm = new QFormLayout(supplierEditor_);
    supplierNameEdit_ = lineEdit(QStringLiteral("supplierNameEdit"), supplierEditor_);
    supplierContactEdit_ = lineEdit(
        QStringLiteral("supplierContactEdit"), supplierEditor_
    );
    supplierPhoneEdit_ = lineEdit(QStringLiteral("supplierPhoneEdit"), supplierEditor_);
    supplierLeadDaysEdit_ = lineEdit(
        QStringLiteral("supplierLeadDaysEdit"), supplierEditor_
    );
    supplierLeadDaysEdit_->setPlaceholderText(QStringLiteral("例如 7"));
    supplierDefaultCheck_ = new QCheckBox(QStringLiteral("默认供应商"), supplierEditor_);
    supplierDefaultCheck_->setObjectName(QStringLiteral("supplierDefaultCheck"));
    supplierSaveButton_ = button(
        QStringLiteral("保存"), QStringLiteral("supplierSaveButton"), supplierEditor_
    );
    supplierCancelButton_ = button(
        QStringLiteral("取消"), QStringLiteral("supplierCancelButton"), supplierEditor_
    );
    supplierForm->addRow(QStringLiteral("供应商名称 *"), supplierNameEdit_);
    supplierForm->addRow(QStringLiteral("联系人"), supplierContactEdit_);
    supplierForm->addRow(QStringLiteral("电话"), supplierPhoneEdit_);
    supplierForm->addRow(QStringLiteral("供货周期（天）"), supplierLeadDaysEdit_);
    supplierForm->addRow(QString(), supplierDefaultCheck_);
    auto* supplierButtons = new QHBoxLayout;
    supplierButtons->addWidget(supplierSaveButton_);
    supplierButtons->addWidget(supplierCancelButton_);
    supplierForm->addRow(supplierButtons);
    supplierEditor_->setEnabled(false);
    supplierLayout->addWidget(supplierEditor_);
    outer->addWidget(supplierGroupBox_);

    // 价格分支（三级向导第 3 步：电线类按铜价区分）
    priceGroupBox_ = new QGroupBox(QStringLiteral("第 3 步 · 价格分支"), materialEditor_);
    priceGroupBox_->setObjectName(QStringLiteral("priceGroupBox"));
    auto* priceLayout = new QVBoxLayout(priceGroupBox_);
    // 向导新增：选择第 2 步确定的供应商（编辑模式下隐藏）。
    priceSupplierCombo_ = new QComboBox(priceGroupBox_);
    priceSupplierCombo_->setObjectName(QStringLiteral("priceSupplierCombo"));
    priceSupplierCombo_->setVisible(false);
    priceLayout->addWidget(priceSupplierCombo_);
    pricesTable_ = new QTableWidget(priceGroupBox_);
    pricesTable_->setObjectName(QStringLiteral("pricesTable"));
    pricesTable_->setColumnCount(6);
    pricesTable_->setHorizontalHeaderLabels({
        QStringLiteral("铜价（元）"),
        QStringLiteral("单价（元）"),
        QStringLiteral("默认"),
        QStringLiteral("状态"),
        QStringLiteral("版本"),
        QStringLiteral("备注"),
    });
    pricesTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    pricesTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    pricesTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    pricesTable_->verticalHeader()->setVisible(false);
    pricesTable_->horizontalHeader()->setStretchLastSection(true);
    priceLayout->addWidget(pricesTable_);

    auto* priceActions = new QHBoxLayout;
    priceAddButton_ = button(
        QStringLiteral("新建价格"), QStringLiteral("priceAddButton"), priceGroupBox_
    );
    priceEditButton_ = button(
        QStringLiteral("编辑选中"), QStringLiteral("priceEditButton"), priceGroupBox_
    );
    priceToggleButton_ = button(
        QStringLiteral("停用选中"), QStringLiteral("priceToggleButton"), priceGroupBox_
    );
    priceActions->addWidget(priceAddButton_);
    priceActions->addWidget(priceEditButton_);
    priceActions->addWidget(priceToggleButton_);
    priceActions->addStretch();
    priceLayout->addLayout(priceActions);

    pricesStatusLabel_ = new QLabel(priceGroupBox_);
    pricesStatusLabel_->setObjectName(QStringLiteral("pricesStatusLabel"));
    pricesStatusLabel_->setWordWrap(true);
    priceLayout->addWidget(pricesStatusLabel_);

    priceEditor_ = new QGroupBox(QStringLiteral("价格信息"), priceGroupBox_);
    priceEditor_->setObjectName(QStringLiteral("priceEditor"));
    auto* priceForm = new QFormLayout(priceEditor_);
    priceCopperEdit_ = lineEdit(QStringLiteral("priceCopperEdit"), priceEditor_);
    priceCopperEdit_->setPlaceholderText(QStringLiteral("例如 7.20（元/千克）"));
    priceUnitEdit_ = lineEdit(QStringLiteral("priceUnitEdit"), priceEditor_);
    priceUnitEdit_->setPlaceholderText(QStringLiteral("例如 12.50"));
    priceDefaultCheck_ = new QCheckBox(QStringLiteral("默认价格"), priceEditor_);
    priceDefaultCheck_->setObjectName(QStringLiteral("priceDefaultCheck"));
    priceSaveButton_ = button(
        QStringLiteral("保存"), QStringLiteral("priceSaveButton"), priceEditor_
    );
    priceCancelButton_ = button(
        QStringLiteral("取消"), QStringLiteral("priceCancelButton"), priceEditor_
    );
    priceForm->addRow(QStringLiteral("铜价（元）"), priceCopperEdit_);
    priceForm->addRow(QStringLiteral("单价（元）*"), priceUnitEdit_);
    priceForm->addRow(QString(), priceDefaultCheck_);
    auto* priceButtons = new QHBoxLayout;
    priceButtons->addWidget(priceSaveButton_);
    priceButtons->addWidget(priceCancelButton_);
    priceForm->addRow(priceButtons);
    priceEditor_->setEnabled(false);
    priceLayout->addWidget(priceEditor_);
    outer->addWidget(priceGroupBox_);

    // 向导完成按钮：第三级确认后统一提交整包（新增向导专属，默认隐藏）。
    bundleCommitButton_ = button(
        QStringLiteral("完成并提交物料库"), QStringLiteral("bundleCommitButton"),
        materialEditor_
    );
    bundleCommitButton_->setVisible(false);
    outer->addWidget(bundleCommitButton_);

    materialEditor_->setEnabled(false);
    return materialEditor_;
}

QWidget* CatalogWidget::createCustomersPage() {
    auto* page = new QWidget(this);
    page->setObjectName(QStringLiteral("customersPage"));
    auto* pageLayout = new QVBoxLayout(page);

    auto* searchLayout = new QHBoxLayout;
    customersSearchEdit_ = lineEdit(QStringLiteral("customersSearchEdit"), page);
    customersSearchEdit_->setPlaceholderText(QStringLiteral("按客户名称、联系人或电话搜索"));
    customersSearchButton_ = button(
        QStringLiteral("搜索"), QStringLiteral("customersSearchButton"), page
    );
    customersRefreshButton_ = button(
        QStringLiteral("刷新"), QStringLiteral("customersRefreshButton"), page
    );
    // 常用操作按钮放在页面顶部，与搜索栏同一行，便于快速访问。
    customerAddButton_ = button(
        QStringLiteral("新增客户"), QStringLiteral("customerAddButton"), page
    );
    customerEditButton_ = button(
        QStringLiteral("编辑客户"), QStringLiteral("customerEditButton"), page
    );
    searchLayout->addWidget(customersSearchEdit_, 1);
    searchLayout->addWidget(customersSearchButton_);
    searchLayout->addWidget(customersRefreshButton_);
    searchLayout->addStretch();
    searchLayout->addWidget(customerAddButton_);
    searchLayout->addWidget(customerEditButton_);
    pageLayout->addLayout(searchLayout);

    auto* splitter = new QSplitter(Qt::Horizontal, page);
    splitter->setObjectName(QStringLiteral("customersSplitter"));
    customersTable_ = new QTableWidget(splitter);
    customersTable_->setObjectName(QStringLiteral("customersTable"));
    customersTable_->setColumnCount(6);
    customersTable_->setHorizontalHeaderLabels({
        QStringLiteral("客户名称"),
        QStringLiteral("联系人"),
        QStringLiteral("电话"),
        QStringLiteral("地址"),
        QStringLiteral("备注"),
        QStringLiteral("版本"),
    });
    customersTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    customersTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    customersTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    customersTable_->verticalHeader()->setVisible(false);
    customersTable_->horizontalHeader()->setStretchLastSection(true);
    splitter->addWidget(customersTable_);
    splitter->addWidget(createCustomerEditor());
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    pageLayout->addWidget(splitter, 1);

    auto* actions = new QHBoxLayout;
    customersPreviousButton_ = button(
        QStringLiteral("上一页"), QStringLiteral("customersPreviousButton"), page
    );
    customersNextButton_ = button(
        QStringLiteral("下一页"), QStringLiteral("customersNextButton"), page
    );
    customersPageLabel_ = new QLabel(page);
    customersPageLabel_->setObjectName(QStringLiteral("customersPageLabel"));
    actions->addStretch();
    actions->addWidget(customersPreviousButton_);
    actions->addWidget(customersPageLabel_);
    actions->addWidget(customersNextButton_);
    pageLayout->addLayout(actions);

    customersStatusLabel_ = new QLabel(page);
    customersStatusLabel_->setObjectName(QStringLiteral("customersStatusLabel"));
    customersStatusLabel_->setWordWrap(true);
    pageLayout->addWidget(customersStatusLabel_);
    return page;
}

QWidget* CatalogWidget::createCustomerEditor() {
    customerEditor_ = new QGroupBox(QStringLiteral("客户信息"), this);
    customerEditor_->setObjectName(QStringLiteral("customerEditor"));
    auto* layout = new QFormLayout(customerEditor_);
    customerNameEdit_ = lineEdit(QStringLiteral("customerNameEdit"), customerEditor_);
    customerContactEdit_ = lineEdit(QStringLiteral("customerContactEdit"), customerEditor_);
    customerPhoneEdit_ = lineEdit(QStringLiteral("customerPhoneEdit"), customerEditor_);
    customerAddressEdit_ = lineEdit(QStringLiteral("customerAddressEdit"), customerEditor_);
    customerNotesEdit_ = lineEdit(QStringLiteral("customerNotesEdit"), customerEditor_);
    customerSaveButton_ = button(
        QStringLiteral("保存"), QStringLiteral("customerSaveButton"), customerEditor_
    );
    customerCancelButton_ = button(
        QStringLiteral("取消"), QStringLiteral("customerCancelButton"), customerEditor_
    );
    layout->addRow(QStringLiteral("客户名称 *"), customerNameEdit_);
    layout->addRow(QStringLiteral("联系人"), customerContactEdit_);
    layout->addRow(QStringLiteral("电话"), customerPhoneEdit_);
    layout->addRow(QStringLiteral("地址"), customerAddressEdit_);
    layout->addRow(QStringLiteral("备注"), customerNotesEdit_);
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(customerSaveButton_);
    buttons->addWidget(customerCancelButton_);
    layout->addRow(buttons);
    customerEditor_->setEnabled(false);
    return customerEditor_;
}

void CatalogWidget::connectActions() {
    connect(materialsSearchButton_, &QPushButton::clicked, this, [this]() {
        materialPage_ = 1;
        loadMaterials();
    });
    connect(materialsSearchEdit_, &QLineEdit::returnPressed, materialsSearchButton_, &QPushButton::click);
    connect(materialsRefreshButton_, &QPushButton::clicked, this, &CatalogWidget::refreshMaterials);
    connect(materialsPreviousButton_, &QPushButton::clicked, this, [this]() {
        if (materialPage_ > 1) {
            --materialPage_;
            loadMaterials();
        }
    });
    connect(materialsNextButton_, &QPushButton::clicked, this, [this]() {
        if (materialPage_ < materialTotalPages_) {
            ++materialPage_;
            loadMaterials();
        }
    });
    connect(materialsTable_, &QTableWidget::itemSelectionChanged, this, &CatalogWidget::updateWriteAccess);
    // 双击表格行直接进入编辑，免去先选中再点"编辑物料"。
    connect(materialsTable_, &QTableWidget::cellDoubleClicked, this,
            [this](int, int) { beginEditMaterial(); });
    connect(materialAddButton_, &QPushButton::clicked, this, &CatalogWidget::beginNewMaterial);
    connect(materialEditButton_, &QPushButton::clicked, this, &CatalogWidget::beginEditMaterial);
    connect(materialToggleButton_, &QPushButton::clicked, this, &CatalogWidget::toggleSelectedMaterial);
    connect(materialSaveButton_, &QPushButton::clicked, this, &CatalogWidget::saveMaterial);
    connect(materialCancelButton_, &QPushButton::clicked, this, &CatalogWidget::cancelMaterialEdit);
    connect(materialCopperCheck_, &QCheckBox::toggled, this, &CatalogWidget::applyCopperVisibility);

    connect(supplierAddButton_, &QPushButton::clicked, this, &CatalogWidget::beginNewSupplier);
    connect(supplierEditButton_, &QPushButton::clicked, this, &CatalogWidget::beginEditSupplier);
    connect(supplierToggleButton_, &QPushButton::clicked, this, &CatalogWidget::toggleSelectedSupplier);
    connect(supplierSaveButton_, &QPushButton::clicked, this, &CatalogWidget::saveSupplier);
    connect(supplierCancelButton_, &QPushButton::clicked, this, &CatalogWidget::cancelSupplierEdit);
    connect(supplierConfirmButton_, &QPushButton::clicked, this, &CatalogWidget::confirmSupplierStep);
    connect(suppliersTable_, &QTableWidget::itemSelectionChanged, this, [this]() {
        if (selectedSupplierRow() >= 0) {
            if (wizardStep_ == 3) {
                // 第三步：让供应商下拉跟随表格选中行，价格列表由下拉统一驱动，
                // 避免"表格选中行"与"下拉索引"双源不一致造成的价格串位/重影。
                const auto row = selectedSupplierRow();
                if (row >= 0 && row < priceSupplierCombo_->count() &&
                    priceSupplierCombo_->currentIndex() != row) {
                    priceSupplierCombo_->blockSignals(true);
                    priceSupplierCombo_->setCurrentIndex(row);
                    priceSupplierCombo_->blockSignals(false);
                }
                renderWizardPrices();
            } else {
                loadPrices();
            }
        }
        updateBranchAccess();
    });

    connect(priceAddButton_, &QPushButton::clicked, this, &CatalogWidget::beginNewPrice);
    connect(priceEditButton_, &QPushButton::clicked, this, &CatalogWidget::beginEditPrice);
    connect(priceToggleButton_, &QPushButton::clicked, this, &CatalogWidget::toggleSelectedPrice);
    connect(priceSaveButton_, &QPushButton::clicked, this, &CatalogWidget::savePrice);
    connect(priceCancelButton_, &QPushButton::clicked, this, &CatalogWidget::cancelPriceEdit);
    connect(pricesTable_, &QTableWidget::itemSelectionChanged, this, &CatalogWidget::updateBranchAccess);
    // 向导：切换供应商下拉时刷新该供应商的价格列表（渲染暂存，不发 API）。
    connect(priceSupplierCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        if (wizardStep_ == 3) {
            renderWizardPrices();
        }
    });
    connect(bundleCommitButton_, &QPushButton::clicked, this, &CatalogWidget::commitBundle);

    connect(customersSearchButton_, &QPushButton::clicked, this, [this]() {
        customerPage_ = 1;
        loadCustomers();
    });
    connect(customersSearchEdit_, &QLineEdit::returnPressed, customersSearchButton_, &QPushButton::click);
    connect(customersRefreshButton_, &QPushButton::clicked, this, &CatalogWidget::refreshCustomers);
    connect(customersPreviousButton_, &QPushButton::clicked, this, [this]() {
        if (customerPage_ > 1) {
            --customerPage_;
            loadCustomers();
        }
    });
    connect(customersNextButton_, &QPushButton::clicked, this, [this]() {
        if (customerPage_ < customerTotalPages_) {
            ++customerPage_;
            loadCustomers();
        }
    });
    connect(customersTable_, &QTableWidget::itemSelectionChanged, this, &CatalogWidget::updateWriteAccess);
    // 双击表格行直接进入编辑，免去先选中再点"编辑客户"。
    connect(customersTable_, &QTableWidget::cellDoubleClicked, this,
            [this](int, int) { beginEditCustomer(); });
    connect(customerAddButton_, &QPushButton::clicked, this, &CatalogWidget::beginNewCustomer);
    connect(customerEditButton_, &QPushButton::clicked, this, &CatalogWidget::beginEditCustomer);
    connect(customerSaveButton_, &QPushButton::clicked, this, &CatalogWidget::saveCustomer);
    connect(customerCancelButton_, &QPushButton::clicked, this, &CatalogWidget::cancelCustomerEdit);
}

void CatalogWidget::updateWriteAccess() {
    const auto writable = canWrite();
    const auto materialSelected = selectedMaterialRow() >= 0;
    const auto customerSelected = selectedCustomerRow() >= 0;
    materialAddButton_->setEnabled(writable && !materialBusy_);
    materialEditButton_->setEnabled(writable && !materialBusy_ && materialSelected);
    materialToggleButton_->setEnabled(writable && !materialBusy_ && materialSelected);
    materialEditor_->setEnabled(writable && materialEditing_ && !materialBusy_);
    customerAddButton_->setEnabled(writable && !customerBusy_);
    customerEditButton_->setEnabled(writable && !customerBusy_ && customerSelected);
    customerEditor_->setEnabled(writable && customerEditing_ && !customerBusy_);

    if (materialSelected) {
        const auto enabled = materials_.at(selectedMaterialRow())
                                 .value(QStringLiteral("isEnabled"))
                                 .toBool();
        materialToggleButton_->setText(
            enabled ? QStringLiteral("停用选中项") : QStringLiteral("启用选中项")
        );
    }
    updateBranchAccess();
}

void CatalogWidget::updateBranchAccess() {
    const auto writable = canWrite();
    // 三级向导模式下，分组整体启用由 updateWizardAccess 控制，
    // 这里仅维护供应商/价格子表单的编辑状态。
    if (wizardStep_ > 0) {
        if (supplierEditor_) {
            supplierEditor_->setEnabled(writable && supplierEditing_ && !supplierBusy_);
        }
        if (priceEditor_) {
            priceEditor_->setEnabled(writable && priceEditing_ && !priceBusy_);
        }
        return;
    }
    const auto editing = materialEditing_ && editingMaterialId_ > 0;
    const auto supplierSelected = selectedSupplierRow() >= 0;
    const auto priceSelected = selectedPriceRow() >= 0;

    supplierGroupBox_->setEnabled(writable && editing && !supplierBusy_ && !priceBusy_);
    priceGroupBox_->setEnabled(
        writable && editing && supplierSelected && !supplierBusy_ && !priceBusy_
    );
    supplierEditor_->setEnabled(writable && supplierEditing_ && !supplierBusy_);
    priceEditor_->setEnabled(writable && priceEditing_ && !priceBusy_);

    if (supplierSelected) {
        const auto enabled = suppliers_.at(selectedSupplierRow())
                                 .value(QStringLiteral("isEnabled"))
                                 .toBool();
        supplierToggleButton_->setText(
            enabled ? QStringLiteral("停用选中") : QStringLiteral("启用选中")
        );
    }
    if (priceSelected) {
        const auto enabled = prices_.at(selectedPriceRow())
                                 .value(QStringLiteral("isEnabled"))
                                 .toBool();
        priceToggleButton_->setText(
            enabled ? QStringLiteral("停用选中") : QStringLiteral("启用选中")
        );
    }
}

void CatalogWidget::applyCopperVisibility() {
    // 价格编辑器中的铜价输入仅对电线类物料可见
    priceCopperEdit_->setVisible(materialCopperCheck_->isChecked());
}

bool CatalogWidget::branchesReady() const {
    return sessionReady() && materialEditing_ && editingMaterialId_ > 0;
}

void CatalogWidget::applySessionState() {
    if (sessionReady()) {
        updateWriteAccess();
        refreshMaterials();
        refreshCustomers();
        return;
    }

    materialEditing_ = false;
    customerEditing_ = false;
    supplierEditing_ = false;
    priceEditing_ = false;
    editingMaterialId_ = 0;
    editingMaterialRevision_ = 0;
    editingSupplierId_ = 0;
    editingSupplierRevision_ = 0;
    editingPriceId_ = 0;
    editingPriceRevision_ = 0;
    editingCustomerId_ = 0;
    editingCustomerRevision_ = 0;
    materials_.clear();
    customers_.clear();
    suppliers_.clear();
    prices_.clear();
    materialsTable_->setRowCount(0);
    customersTable_->setRowCount(0);
    suppliersTable_->setRowCount(0);
    pricesTable_->setRowCount(0);
    materialPage_ = 1;
    materialTotalPages_ = 0;
    materialTotal_ = 0;
    customerPage_ = 1;
    customerTotalPages_ = 0;
    customerTotal_ = 0;
    materialBusy_ = false;
    customerBusy_ = false;
    supplierBusy_ = false;
    priceBusy_ = false;

    const auto mustChangePassword =
        apiClient_ && apiClient_->isAuthenticated() &&
        apiClient_->session()
            .user
            .value(QStringLiteral("mustChangePassword"))
            .toBool(true);
    const auto materialMessage = mustChangePassword
                                     ? QStringLiteral("请先修改临时密码，再查看物料。")
                                     : QStringLiteral("请先登录后查看物料。");
    const auto customerMessage = mustChangePassword
                                     ? QStringLiteral("请先修改临时密码，再查看客户。")
                                     : QStringLiteral("请先登录后查看客户。");
    materialsStatusLabel_->setText(materialMessage);
    customersStatusLabel_->setText(customerMessage);
    materialEditor_->setTitle(QStringLiteral("物料信息"));
    customerEditor_->setTitle(QStringLiteral("客户信息"));
    setMaterialBusy(false);
    setCustomerBusy(false);
}

bool CatalogWidget::sessionReady() const {
    return apiClient_ && apiClient_->isAuthenticated() &&
           !apiClient_->session()
                .user
                .value(QStringLiteral("mustChangePassword"))
                .toBool(true);
}

bool CatalogWidget::canWrite() const {
    return sessionReady() &&
           apiClient_->session().user.value(QStringLiteral("role")).toString() ==
               QStringLiteral("admin");
}

void CatalogWidget::refreshMaterials() {
    materialPage_ = 1;
    loadMaterials();
}

void CatalogWidget::refreshCustomers() {
    customerPage_ = 1;
    loadCustomers();
}

void CatalogWidget::loadMaterials() {
    if (!sessionReady()) {
        materialsStatusLabel_->setText(
            apiClient_ && apiClient_->isAuthenticated()
                ? QStringLiteral("请先修改临时密码，再查看物料。")
                : QStringLiteral("请先登录后查看物料。")
        );
        return;
    }
    if (materialBusy_) {
        return;
    }
    setMaterialBusy(true);
    materialsStatusLabel_->setText(QStringLiteral("正在加载物料…"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("page"), QString::number(materialPage_));
    query.addQueryItem(QStringLiteral("pageSize"), QString::number(kPageSize));
    const auto search = materialsSearchEdit_->text().trimmed();
    if (!search.isEmpty()) {
        query.addQueryItem(QStringLiteral("search"), search);
    }
    const auto path = QStringLiteral("/api/v1/materials?%1")
                          .arg(query.toString(QUrl::FullyEncoded));
    apiClient_->get(path, [self = QPointer<CatalogWidget>(this)](ApiResponse response) {
        if (self) {
            self->showMaterials(response);
        }
    });
}

void CatalogWidget::loadCustomers() {
    if (!sessionReady()) {
        customersStatusLabel_->setText(
            apiClient_ && apiClient_->isAuthenticated()
                ? QStringLiteral("请先修改临时密码，再查看客户。")
                : QStringLiteral("请先登录后查看客户。")
        );
        return;
    }
    if (customerBusy_) {
        return;
    }
    setCustomerBusy(true);
    customersStatusLabel_->setText(QStringLiteral("正在加载客户…"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("page"), QString::number(customerPage_));
    query.addQueryItem(QStringLiteral("pageSize"), QString::number(kPageSize));
    const auto search = customersSearchEdit_->text().trimmed();
    if (!search.isEmpty()) {
        query.addQueryItem(QStringLiteral("search"), search);
    }
    const auto path = QStringLiteral("/api/v1/customers?%1")
                          .arg(query.toString(QUrl::FullyEncoded));
    apiClient_->get(path, [self = QPointer<CatalogWidget>(this)](ApiResponse response) {
        if (self) {
            self->showCustomers(response);
        }
    });
}

void CatalogWidget::showMaterials(const ApiResponse& response) {
    setMaterialBusy(false);
    if (!sessionReady()) {
        materialsStatusLabel_->setText(QStringLiteral("请先完成登录和临时密码修改。"));
        return;
    }
    if (!response.succeeded()) {
        materialsStatusLabel_->setText(errorText(response));
        return;
    }
    const auto items = response.body.value(QStringLiteral("items"));
    if (!items.isArray()) {
        materialsStatusLabel_->setText(QStringLiteral("服务器返回的物料列表格式不正确。"));
        return;
    }

    materials_.clear();
    materialsTable_->setRowCount(0);
    for (const auto& value : items.toArray()) {
        if (!value.isObject()) {
            continue;
        }
        const auto material = value.toObject();
        const auto row = materialsTable_->rowCount();
        materials_.append(material);
        materialsTable_->insertRow(row);
        materialsTable_->setItem(row, 0, readOnlyItem(material.value(QStringLiteral("code")).toString()));
        materialsTable_->setItem(row, 1, readOnlyItem(material.value(QStringLiteral("name")).toString()));
        materialsTable_->setItem(row, 2, readOnlyItem(material.value(QStringLiteral("specification")).toString()));
        materialsTable_->setItem(row, 3, readOnlyItem(material.value(QStringLiteral("unit")).toString()));
        materialsTable_->setItem(row, 4, readOnlyItem(material.value(QStringLiteral("category")).toString()));
        materialsTable_->setItem(row, 5, readOnlyItem(material.value(QStringLiteral("isCopperBased")).toBool() ? QStringLiteral("是") : QStringLiteral("否")));
        materialsTable_->setItem(row, 6, readOnlyItem(priceText(material.value(QStringLiteral("currentUnitPriceCents")).toInteger())));
        materialsTable_->setItem(row, 7, readOnlyItem(material.value(QStringLiteral("isEnabled")).toBool() ? QStringLiteral("启用") : QStringLiteral("停用")));
        materialsTable_->setItem(row, 8, readOnlyItem(QString::number(material.value(QStringLiteral("revision")).toInteger())));
    }
    materialPage_ = qMax(1, response.body.value(QStringLiteral("page")).toInt(1));
    materialTotalPages_ = qMax(0, response.body.value(QStringLiteral("totalPages")).toInt());
    materialTotal_ = qMax<qint64>(0, response.body.value(QStringLiteral("total")).toInteger());
    materialsStatusLabel_->setText(QStringLiteral("已加载 %1 条物料。")
                                       .arg(materials_.size()));
    updateMaterialPaging();
    updateWriteAccess();
}

void CatalogWidget::showCustomers(const ApiResponse& response) {
    setCustomerBusy(false);
    if (!sessionReady()) {
        customersStatusLabel_->setText(QStringLiteral("请先完成登录和临时密码修改。"));
        return;
    }
    if (!response.succeeded()) {
        customersStatusLabel_->setText(errorText(response));
        return;
    }
    const auto items = response.body.value(QStringLiteral("items"));
    if (!items.isArray()) {
        customersStatusLabel_->setText(QStringLiteral("服务器返回的客户列表格式不正确。"));
        return;
    }

    customers_.clear();
    customersTable_->setRowCount(0);
    for (const auto& value : items.toArray()) {
        if (!value.isObject()) {
            continue;
        }
        const auto customer = value.toObject();
        const auto row = customersTable_->rowCount();
        customers_.append(customer);
        customersTable_->insertRow(row);
        customersTable_->setItem(row, 0, readOnlyItem(customer.value(QStringLiteral("name")).toString()));
        customersTable_->setItem(row, 1, readOnlyItem(customer.value(QStringLiteral("contactName")).toString()));
        customersTable_->setItem(row, 2, readOnlyItem(customer.value(QStringLiteral("phone")).toString()));
        customersTable_->setItem(row, 3, readOnlyItem(customer.value(QStringLiteral("address")).toString()));
        customersTable_->setItem(row, 4, readOnlyItem(customer.value(QStringLiteral("notes")).toString()));
        customersTable_->setItem(row, 5, readOnlyItem(QString::number(customer.value(QStringLiteral("revision")).toInteger())));
    }
    customerPage_ = qMax(1, response.body.value(QStringLiteral("page")).toInt(1));
    customerTotalPages_ = qMax(0, response.body.value(QStringLiteral("totalPages")).toInt());
    customerTotal_ = qMax<qint64>(0, response.body.value(QStringLiteral("total")).toInteger());
    customersStatusLabel_->setText(QStringLiteral("已加载 %1 条客户。")
                                       .arg(customers_.size()));
    updateCustomerPaging();
    updateWriteAccess();
}

void CatalogWidget::updateMaterialPaging() {
    const auto shownTotalPages = qMax(1, materialTotalPages_);
    materialsPageLabel_->setText(
        QStringLiteral("第 %1 / %2 页，共 %3 条")
            .arg(materialPage_)
            .arg(shownTotalPages)
            .arg(materialTotal_)
    );
    materialsPreviousButton_->setEnabled(
        sessionReady() && !materialBusy_ && materialPage_ > 1
    );
    materialsNextButton_->setEnabled(
        sessionReady() && !materialBusy_ && materialTotalPages_ > 0 &&
        materialPage_ < materialTotalPages_
    );
}

void CatalogWidget::updateCustomerPaging() {
    const auto shownTotalPages = qMax(1, customerTotalPages_);
    customersPageLabel_->setText(
        QStringLiteral("第 %1 / %2 页，共 %3 条")
            .arg(customerPage_)
            .arg(shownTotalPages)
            .arg(customerTotal_)
    );
    customersPreviousButton_->setEnabled(
        sessionReady() && !customerBusy_ && customerPage_ > 1
    );
    customersNextButton_->setEnabled(
        sessionReady() && !customerBusy_ && customerTotalPages_ > 0 &&
        customerPage_ < customerTotalPages_
    );
}

void CatalogWidget::beginWizard() {
    if (!canWrite()) {
        return;
    }
    editingMaterialId_ = 0;
    editingMaterialRevision_ = 0;
    materialEditing_ = true;
    wizardStep_ = 1;
    bundleDraft_ = QJsonObject{
        {QStringLiteral("material"), QJsonObject{}},
        {QStringLiteral("suppliers"), QJsonArray{}},
    };
    materialCodeEdit_->clear();
    materialNameEdit_->clear();
    materialSpecificationEdit_->clear();
    materialUnitEdit_->clear();
    materialCategoryEdit_->clear();
    materialCopperCheck_->setChecked(false);
    materialPriceEdit_->clear();
    materialEnabledCheck_->setChecked(true);
    materialEditor_->setTitle(QStringLiteral("新增物料（三级向导）"));
    suppliers_.clear();
    suppliersTable_->setRowCount(0);
    prices_.clear();
    pricesTable_->setRowCount(0);
    editingSupplierId_ = 0;
    editingPriceId_ = 0;
    supplierEditing_ = false;
    priceEditing_ = false;
    materialBaseGroup_->setEnabled(true);
    supplierGroupBox_->setEnabled(false);
    priceGroupBox_->setEnabled(false);
    bundleCommitButton_->setVisible(true);
    supplierConfirmButton_->setVisible(true);
    priceSupplierCombo_->setVisible(true);
    updateWriteAccess();
    materialCodeEdit_->setFocus();
    applyCopperVisibility();
}

void CatalogWidget::beginNewMaterial() {
    if (!canWrite()) {
        return;
    }
    beginWizard();
}

void CatalogWidget::beginEditMaterial() {
    const auto row = selectedMaterialRow();
    if (!canWrite() || row < 0) {
        return;
    }
    const auto material = materials_.at(row);
    editingMaterialId_ = material.value(QStringLiteral("id")).toInteger();
    editingMaterialRevision_ = material.value(QStringLiteral("revision")).toInteger();
    materialEditing_ = true;
    // 编辑模式与新增一致：三级向导，全部确认后整包提交。
    wizardStep_ = 1;
    bundleDraft_ = QJsonObject{
        {QStringLiteral("material"), QJsonObject{}},
        {QStringLiteral("suppliers"), QJsonArray{}},
    };
    materialCodeEdit_->setText(material.value(QStringLiteral("code")).toString());
    materialNameEdit_->setText(material.value(QStringLiteral("name")).toString());
    materialSpecificationEdit_->setText(material.value(QStringLiteral("specification")).toString());
    materialUnitEdit_->setText(material.value(QStringLiteral("unit")).toString());
    materialCategoryEdit_->setText(material.value(QStringLiteral("category")).toString());
    materialCopperCheck_->setChecked(material.value(QStringLiteral("isCopperBased")).toBool());
    materialPriceEdit_->setText(priceText(material.value(QStringLiteral("currentUnitPriceCents")).toInteger()));
    materialEnabledCheck_->setChecked(material.value(QStringLiteral("isEnabled")).toBool());
    materialEditor_->setTitle(QStringLiteral("编辑物料（三级向导）"));
    materialBaseGroup_->setEnabled(true);
    supplierGroupBox_->setEnabled(false);
    priceGroupBox_->setEnabled(false);
    bundleCommitButton_->setVisible(true);
    supplierConfirmButton_->setVisible(true);
    priceSupplierCombo_->setVisible(true);
    suppliers_.clear();
    suppliersTable_->setRowCount(0);
    prices_.clear();
    pricesTable_->setRowCount(0);
    editingSupplierId_ = 0;
    editingPriceId_ = 0;
    supplierEditing_ = false;
    priceEditing_ = false;
    updateWriteAccess();
    applyCopperVisibility();
    // 加载该物料现有供应商与价格作为向导起点。
    loadSuppliers();
}

void CatalogWidget::saveMaterial() {
    if (!canWrite() || !materialEditing_ || materialBusy_) {
        return;
    }
    const auto code = materialCodeEdit_->text().trimmed();
    const auto name = materialNameEdit_->text().trimmed();
    const auto unit = materialUnitEdit_->text().trimmed();
    const auto cents = parsePrice(materialPriceEdit_->text());
    if (code.isEmpty() || name.isEmpty() || unit.isEmpty()) {
        materialsStatusLabel_->setText(QStringLiteral("请填写物料编码、名称和单位。"));
        return;
    }
    if (!cents.has_value()) {
        materialsStatusLabel_->setText(
            QStringLiteral("单价请输入非负金额，最多保留两位小数，例如 12.50。")
        );
        return;
    }

    // 三级向导第 1 步：仅暂存到内存草稿，不落库；随后锁定基本信息、解锁供应商。
    if (wizardStep_ == 1) {
        bundleDraft_.insert(QStringLiteral("material"), QJsonObject{
            {QStringLiteral("code"), code},
            {QStringLiteral("name"), name},
            {QStringLiteral("specification"), materialSpecificationEdit_->text().trimmed()},
            {QStringLiteral("unit"), unit},
            {QStringLiteral("category"), materialCategoryEdit_->text().trimmed()},
            {QStringLiteral("isCopperBased"), materialCopperCheck_->isChecked()},
            {QStringLiteral("currentUnitPriceCents"), *cents},
            {QStringLiteral("isEnabled"), materialEnabledCheck_->isChecked()},
        });
        confirmMaterialStep();
        return;
    }

    QJsonObject body{
        {QStringLiteral("code"), code},
        {QStringLiteral("name"), name},
        {QStringLiteral("specification"), materialSpecificationEdit_->text().trimmed()},
        {QStringLiteral("unit"), unit},
        {QStringLiteral("category"), materialCategoryEdit_->text().trimmed()},
        {QStringLiteral("isCopperBased"), materialCopperCheck_->isChecked()},
        {QStringLiteral("currentUnitPriceCents"), *cents},
        {QStringLiteral("isEnabled"), materialEnabledCheck_->isChecked()},
    };
    const auto updating = editingMaterialId_ > 0;
    if (updating) {
        body.insert(QStringLiteral("revision"), editingMaterialRevision_);
    }
    setMaterialBusy(true);
    materialsStatusLabel_->setText(QStringLiteral("正在保存物料…"));
    const auto callback = [self = QPointer<CatalogWidget>(this), updating](ApiResponse response) {
        if (!self) {
            return;
        }
        self->setMaterialBusy(false);
        if (!response.succeeded()) {
            self->materialsStatusLabel_->setText(self->errorText(response));
            return;
        }
        const auto created = response.body;
        self->editingMaterialId_ = created.value(QStringLiteral("id")).toInteger();
        self->editingMaterialRevision_ = created.value(QStringLiteral("revision")).toInteger();
        self->materialsStatusLabel_->setText(
            updating ? QStringLiteral("物料修改成功。") : QStringLiteral("物料新增成功。")
        );
        if (!updating) {
            // 新增后进入编辑态，可直接添加供应商分支
            self->materialEditor_->setTitle(QStringLiteral("编辑物料"));
            self->suppliers_.clear();
            self->suppliersTable_->setRowCount(0);
            self->prices_.clear();
            self->pricesTable_->setRowCount(0);
        }
        self->updateWriteAccess();
        self->loadMaterials();
        self->loadSuppliers();
    };
    if (updating) {
        apiClient_->put(
            QStringLiteral("/api/v1/materials/%1").arg(editingMaterialId_),
            body,
            callback
        );
    } else {
        apiClient_->post(QStringLiteral("/api/v1/materials"), body, callback);
    }
}

// 向导第 1 步确认：基本信息已暂存，锁定该级并解锁供应商级。
void CatalogWidget::confirmMaterialStep() {
    if (wizardStep_ != 1) {
        return;
    }
    wizardStep_ = 2;
    materialBaseGroup_->setEnabled(false);
    supplierGroupBox_->setEnabled(true);
    priceGroupBox_->setEnabled(false);
    suppliers_.clear();
    suppliersTable_->setRowCount(0);
    prices_.clear();
    pricesTable_->setRowCount(0);
    editingSupplierId_ = 0;
    editingPriceId_ = 0;
    supplierEditing_ = false;
    priceEditing_ = false;
    // 编辑模式：第 1 步确认后展示该物料已加载的供应商草稿。
    if (editingMaterialId_ > 0) {
        renderWizardSuppliers();
        materialsStatusLabel_->setText(
            QStringLiteral("物料信息已确认。可修改现有供应商或继续添加。")
        );
    } else {
        materialsStatusLabel_->setText(
            QStringLiteral("物料信息已确认（暂存，尚未写入数据库）。请继续添加供应商。")
        );
    }
    updateWizardAccess();
    supplierNameEdit_->setFocus();
}

// 向导第 2 步确认：供应商已确定，锁定该级并解锁价格级。
void CatalogWidget::confirmSupplierStep() {
    if (wizardStep_ != 2) {
        return;
    }
    const auto suppliers = bundleDraft_.value(QStringLiteral("suppliers")).toArray();
    if (suppliers.isEmpty()) {
        materialsStatusLabel_->setText(QStringLiteral("请至少添加一个供应商后再进入价格设置。"));
        return;
    }
    wizardStep_ = 3;
    materialBaseGroup_->setEnabled(false);
    // 第三步保留第二步可编辑：供应商可继续调整，改动实时同步。
    supplierGroupBox_->setEnabled(true);
    priceGroupBox_->setEnabled(true);
    // 价格级：供应商下拉列出第 2 步确定的全部供应商。
    priceSupplierCombo_->clear();
    suppliers_.clear();
    for (int index = 0; index < suppliers.size(); ++index) {
        const auto entry = suppliers.at(index).toObject();
        const auto supplier = entry.value(QStringLiteral("supplier")).toObject();
        priceSupplierCombo_->addItem(
            supplier.value(QStringLiteral("supplierName")).toString()
        );
        // 向导内用数组索引充当 id，供第 3 步按供应商定位草稿。
        auto supplierWithIndex = supplier;
        supplierWithIndex.insert(QStringLiteral("id"), index);
        suppliers_.append(supplierWithIndex);
    }
    suppliersTable_->setRowCount(0);
    prices_.clear();
    pricesTable_->setRowCount(0);
    supplierEditing_ = false;
    priceEditing_ = false;
    materialsStatusLabel_->setText(
        QStringLiteral("供应商已确定。请为每个供应商设置价格，然后点击“完成并提交物料库”。")
    );
    updateWizardAccess();
    renderWizardPrices();
}

// 向导状态下的访问控制：三级逐步解锁、前级锁定。
void CatalogWidget::updateWizardAccess() {
    const auto writable = canWrite() && !materialBusy_ && !supplierBusy_ && !priceBusy_;
    if (wizardStep_ == 1) {
        materialBaseGroup_->setEnabled(writable);
        supplierGroupBox_->setEnabled(false);
        priceGroupBox_->setEnabled(false);
        bundleCommitButton_->setEnabled(false);
    } else if (wizardStep_ == 2) {
        materialBaseGroup_->setEnabled(false);
        supplierGroupBox_->setEnabled(writable);
        priceGroupBox_->setEnabled(false);
        bundleCommitButton_->setEnabled(false);
    } else if (wizardStep_ == 3) {
        materialBaseGroup_->setEnabled(false);
        // 第三步允许回到第二步调整供应商：供应商改动会实时同步到第三步。
        supplierGroupBox_->setEnabled(writable);
        priceGroupBox_->setEnabled(writable);
        bundleCommitButton_->setEnabled(
            writable && priceSupplierCombo_->count() > 0
        );
    }
}

// 向导整包提交：物料 + 供应商 + 价格一次性写入数据库。
void CatalogWidget::commitBundle() {
    if (!canWrite() || wizardStep_ != 3 || materialBusy_) {
        return;
    }
    const auto updating = editingMaterialId_ > 0;
    setMaterialBusy(true);
    materialsStatusLabel_->setText(QStringLiteral("正在提交整包物料库…"));
    const auto callback = [self = QPointer<CatalogWidget>(this), updating](ApiResponse response) {
        if (!self) {
            return;
        }
        self->setMaterialBusy(false);
        if (!response.succeeded()) {
            self->materialsStatusLabel_->setText(self->errorText(response));
            return;
        }
        self->wizardStep_ = 0;
        self->bundleDraft_ = QJsonObject{};
        self->bundleCommitButton_->setVisible(false);
        self->priceSupplierCombo_->setVisible(false);
        self->cancelMaterialEdit();
        self->materialsStatusLabel_->setText(
            updating ? QStringLiteral("物料库更新成功：物料、供应商与价格已写入数据库。")
                     : QStringLiteral("物料库提交成功：物料、供应商与价格已写入数据库。")
        );
        self->refreshMaterials();
    };
    // 编辑模式整包替换（PUT，带乐观锁）；新增模式整包创建（POST）。
    if (updating) {
        auto body = bundleDraft_;
        body.insert(QStringLiteral("materialId"), editingMaterialId_);
        body.insert(
            QStringLiteral("materialRevision"),
            static_cast<qint64>(editingMaterialRevision_)
        );
        apiClient_->put(
            QStringLiteral("/api/v1/materials/bundle"),
            body,
            callback
        );
    } else {
        apiClient_->post(
            QStringLiteral("/api/v1/materials/bundle"),
            bundleDraft_,
            callback
        );
    }
}

void CatalogWidget::cancelMaterialEdit() {
    materialEditing_ = false;
    editingMaterialId_ = 0;
    editingMaterialRevision_ = 0;
    wizardStep_ = 0;
    bundleDraft_ = QJsonObject{};
    bundleCommitButton_->setVisible(false);
    priceSupplierCombo_->setVisible(false);
    supplierConfirmButton_->setVisible(false);
    materialBaseGroup_->setEnabled(true);
    materialEditor_->setTitle(QStringLiteral("物料信息"));
    suppliers_.clear();
    suppliersTable_->setRowCount(0);
    prices_.clear();
    pricesTable_->setRowCount(0);
    supplierEditing_ = false;
    priceEditing_ = false;
    updateWriteAccess();
}

void CatalogWidget::toggleSelectedMaterial() {
    const auto row = selectedMaterialRow();
    if (!canWrite() || row < 0 || materialBusy_) {
        return;
    }
    const auto material = materials_.at(row);
    const auto enabled = material.value(QStringLiteral("isEnabled")).toBool();
    const QJsonObject body{
        {QStringLiteral("revision"), material.value(QStringLiteral("revision")).toInteger()},
        {QStringLiteral("isEnabled"), !enabled},
    };
    setMaterialBusy(true);
    materialsStatusLabel_->setText(
        enabled ? QStringLiteral("正在停用物料…") : QStringLiteral("正在启用物料…")
    );
    apiClient_->patch(
        QStringLiteral("/api/v1/materials/%1/enabled")
            .arg(material.value(QStringLiteral("id")).toInteger()),
        body,
        [self = QPointer<CatalogWidget>(this)](ApiResponse response) {
            if (!self) {
                return;
            }
            self->setMaterialBusy(false);
            if (!response.succeeded()) {
                self->materialsStatusLabel_->setText(self->errorText(response));
                return;
            }
            self->loadMaterials();
        }
    );
}

void CatalogWidget::loadSuppliers() {
    if (!branchesReady() || supplierBusy_) {
        return;
    }
    setSupplierBusy(true);
    suppliersStatusLabel_->setText(QStringLiteral("正在加载供应商…"));
    const auto path = QStringLiteral("/api/v1/materials/%1/suppliers")
                          .arg(editingMaterialId_);
    apiClient_->get(path, [self = QPointer<CatalogWidget>(this)](ApiResponse response) {
        if (self) {
            self->showSuppliers(response);
        }
    });
}

void CatalogWidget::showSuppliers(const ApiResponse& response) {
    setSupplierBusy(false);
    if (!branchesReady()) {
        return;
    }
    if (!response.succeeded()) {
        suppliersStatusLabel_->setText(errorText(response));
        updateBranchAccess();
        return;
    }
    const auto items = response.body.value(QStringLiteral("items"));
    if (!items.isArray()) {
        suppliersStatusLabel_->setText(QStringLiteral("服务器返回的供应商列表格式不正确。"));
        updateBranchAccess();
        return;
    }

    suppliers_.clear();
    suppliersTable_->setRowCount(0);
    auto draftSuppliers = bundleDraft_.value(QStringLiteral("suppliers")).toArray();
    draftSuppliers = QJsonArray{};
    for (const auto& value : items.toArray()) {
        if (!value.isObject()) {
            continue;
        }
        const auto supplier = value.toObject();
        const auto row = suppliersTable_->rowCount();
        suppliers_.append(supplier);
        suppliersTable_->insertRow(row);
        suppliersTable_->setItem(row, 0, readOnlyItem(supplier.value(QStringLiteral("supplierName")).toString()));
        suppliersTable_->setItem(row, 1, readOnlyItem(supplier.value(QStringLiteral("contactName")).toString()));
        suppliersTable_->setItem(row, 2, readOnlyItem(supplier.value(QStringLiteral("phone")).toString()));
        suppliersTable_->setItem(row, 3, readOnlyItem(QString::number(supplier.value(QStringLiteral("leadDays")).toInt())));
        suppliersTable_->setItem(row, 4, readOnlyItem(supplier.value(QStringLiteral("isDefault")).toBool() ? QStringLiteral("是") : QStringLiteral("否")));
        suppliersTable_->setItem(row, 5, readOnlyItem(supplier.value(QStringLiteral("isEnabled")).toBool() ? QStringLiteral("启用") : QStringLiteral("停用")));
        suppliersTable_->setItem(row, 6, readOnlyItem(QString::number(supplier.value(QStringLiteral("revision")).toInteger())));
        // 向导编辑起点：供应商草稿带 id/revision，价格稍后异步填充。
        // 编辑模式下加载可能晚于用户操作完成，故向导任意步骤都写回草稿。
        if (wizardStep_ == 1 || (wizardStep_ == 2 && editingMaterialId_ > 0)) {
            draftSuppliers.append(QJsonObject{
                {QStringLiteral("supplier"), supplier},
                {QStringLiteral("prices"), QJsonArray{}},
            });
        }
    }
    if (wizardStep_ == 1 || (wizardStep_ == 2 && editingMaterialId_ > 0)) {
        bundleDraft_.insert(QStringLiteral("suppliers"), draftSuppliers);
    }
    suppliersStatusLabel_->setText(QStringLiteral("该物料共 %1 个供应商。")
                                       .arg(suppliers_.size()));
    prices_.clear();
    pricesTable_->setRowCount(0);
    editingPriceId_ = 0;
    priceEditing_ = false;
    updateBranchAccess();
}

void CatalogWidget::beginNewSupplier() {
    if (!canWrite()) {
        return;
    }
    if (wizardStep_ != 2 && !branchesReady()) {
        return;
    }
    editingSupplierId_ = 0;
    editingSupplierRevision_ = 0;
    supplierEditing_ = true;
    supplierNameEdit_->clear();
    supplierContactEdit_->clear();
    supplierPhoneEdit_->clear();
    supplierLeadDaysEdit_->clear();
    supplierDefaultCheck_->setChecked(false);
    supplierEditor_->setTitle(QStringLiteral("新增供应商"));
    updateBranchAccess();
    supplierNameEdit_->setFocus();
}

void CatalogWidget::beginEditSupplier() {
    const auto row = selectedSupplierRow();
    if (!canWrite() || !branchesReady() || row < 0) {
        return;
    }
    const auto supplier = suppliers_.at(row);
    editingSupplierId_ = supplier.value(QStringLiteral("id")).toInteger();
    editingSupplierRevision_ = supplier.value(QStringLiteral("revision")).toInteger();
    supplierEditing_ = true;
    supplierNameEdit_->setText(supplier.value(QStringLiteral("supplierName")).toString());
    supplierContactEdit_->setText(supplier.value(QStringLiteral("contactName")).toString());
    supplierPhoneEdit_->setText(supplier.value(QStringLiteral("phone")).toString());
    supplierLeadDaysEdit_->setText(
        QString::number(supplier.value(QStringLiteral("leadDays")).toInt())
    );
    supplierDefaultCheck_->setChecked(supplier.value(QStringLiteral("isDefault")).toBool());
    supplierEditor_->setTitle(QStringLiteral("编辑供应商"));
    updateBranchAccess();
}

void CatalogWidget::saveSupplier() {
    if (!canWrite() || !supplierEditing_ || supplierBusy_) {
        return;
    }
    const auto name = supplierNameEdit_->text().trimmed();
    if (name.isEmpty()) {
        suppliersStatusLabel_->setText(QStringLiteral("请填写供应商名称。"));
        return;
    }
    const auto leadDaysText = supplierLeadDaysEdit_->text().trimmed();
    int leadDays = 0;
    if (!leadDaysText.isEmpty()) {
        bool ok = false;
        const auto parsed = leadDaysText.toInt(&ok);
        if (!ok || parsed < 0 || parsed > 36'500) {
            suppliersStatusLabel_->setText(
                QStringLiteral("供货周期请输入 0-36500 的整数天数。")
            );
            return;
        }
        leadDays = parsed;
    }
    QJsonObject body{
        {QStringLiteral("supplierName"), name},
        {QStringLiteral("contactName"), supplierContactEdit_->text().trimmed()},
        {QStringLiteral("phone"), supplierPhoneEdit_->text().trimmed()},
        {QStringLiteral("leadDays"), leadDays},
        {QStringLiteral("isDefault"), supplierDefaultCheck_->isChecked()},
        {QStringLiteral("isEnabled"), true},
    };

    // 三级向导第 2 步：供应商暂存到内存草稿，不落库。
    if (wizardStep_ == 2) {
        auto suppliers = bundleDraft_.value(QStringLiteral("suppliers")).toArray();
        // 编辑模式：修改已有供应商时更新原条目（保留其 id/revision）。
        const auto editingExisting = editingSupplierId_ > 0;
        if (editingExisting) {
            for (int index = 0; index < suppliers.size(); ++index) {
                const auto existing =
                    suppliers.at(index).toObject()
                        .value(QStringLiteral("supplier")).toObject();
                if (existing.value(QStringLiteral("id")).toInteger() == editingSupplierId_) {
                    auto updated = existing;
                    for (auto it = body.constBegin(); it != body.constEnd(); ++it) {
                        updated.insert(it.key(), it.value());
                    }
                    auto entry = suppliers.at(index).toObject();
                    entry.insert(QStringLiteral("supplier"), updated);
                    suppliers.replace(index, entry);
                    bundleDraft_.insert(QStringLiteral("suppliers"), suppliers);
                    cancelSupplierEdit();
                    renderWizardSuppliers();
                    suppliersStatusLabel_->setText(
                        QStringLiteral("供应商已更新（暂存，尚未写入数据库）。")
                    );
                    return;
                }
            }
        }
        const auto duplicate = std::any_of(
            suppliers.begin(),
            suppliers.end(),
            [&](const QJsonValue& value) {
                return value.toObject()
                           .value(QStringLiteral("supplier"))
                           .toObject()
                           .value(QStringLiteral("supplierName"))
                           .toString() == name;
            }
        );
        if (duplicate) {
            suppliersStatusLabel_->setText(QStringLiteral("该供应商已添加，请勿重复。"));
            return;
        }
        suppliers.append(QJsonObject{
            {QStringLiteral("supplier"), body},
            {QStringLiteral("prices"), QJsonArray{}},
        });
        bundleDraft_.insert(QStringLiteral("suppliers"), suppliers);
        cancelSupplierEdit();
        renderWizardSuppliers();
        suppliersStatusLabel_->setText(
            QStringLiteral("供应商已暂存，可继续添加；确认后点击“确定供应商”。")
        );
        return;
    }

    const auto updating = editingSupplierId_ > 0;
    if (updating) {
        body.insert(QStringLiteral("revision"), editingSupplierRevision_);
    }
    setSupplierBusy(true);
    suppliersStatusLabel_->setText(QStringLiteral("正在保存供应商…"));
    const auto callback = [self = QPointer<CatalogWidget>(this), updating](ApiResponse response) {
        if (!self) {
            return;
        }
        self->setSupplierBusy(false);
        if (!response.succeeded()) {
            self->suppliersStatusLabel_->setText(self->errorText(response));
            return;
        }
        self->cancelSupplierEdit();
        self->suppliersStatusLabel_->setText(
            updating ? QStringLiteral("供应商修改成功。")
                     : QStringLiteral("供应商新增成功，可继续新建第二、第三…个供应商。")
        );
        self->loadSuppliers();
    };
    const auto path = updating
                          ? QStringLiteral("/api/v1/materials/%1/suppliers/%2")
                                .arg(editingMaterialId_)
                                .arg(editingSupplierId_)
                          : QStringLiteral("/api/v1/materials/%1/suppliers")
                                .arg(editingMaterialId_);
    if (updating) {
        apiClient_->put(path, body, callback);
    } else {
        apiClient_->post(path, body, callback);
    }
}

void CatalogWidget::cancelSupplierEdit() {
    supplierEditing_ = false;
    editingSupplierId_ = 0;
    editingSupplierRevision_ = 0;
    supplierEditor_->setTitle(QStringLiteral("供应商信息"));
    updateBranchAccess();
}

// 向导第 2 步：把暂存草稿中的供应商渲染到表格（尚未落库）。
void CatalogWidget::renderWizardSuppliers() {
    suppliers_.clear();
    suppliersTable_->setRowCount(0);
    const auto suppliers =
        bundleDraft_.value(QStringLiteral("suppliers")).toArray();
    for (const auto& value : suppliers) {
        const auto supplier =
            value.toObject().value(QStringLiteral("supplier")).toObject();
        const auto row = suppliersTable_->rowCount();
        suppliers_.append(supplier);
        suppliersTable_->insertRow(row);
        suppliersTable_->setItem(row, 0, readOnlyItem(supplier.value(QStringLiteral("supplierName")).toString()));
        suppliersTable_->setItem(row, 1, readOnlyItem(supplier.value(QStringLiteral("contactName")).toString()));
        suppliersTable_->setItem(row, 2, readOnlyItem(supplier.value(QStringLiteral("phone")).toString()));
        suppliersTable_->setItem(row, 3, readOnlyItem(QString::number(supplier.value(QStringLiteral("leadDays")).toInt())));
        suppliersTable_->setItem(row, 4, readOnlyItem(supplier.value(QStringLiteral("isDefault")).toBool() ? QStringLiteral("是") : QStringLiteral("否")));
        suppliersTable_->setItem(row, 5, readOnlyItem(QStringLiteral("暂存")));
        suppliersTable_->setItem(row, 6, readOnlyItem(QStringLiteral("-")));
    }
    suppliersStatusLabel_->setText(
        QStringLiteral("已暂存 %1 个供应商（尚未写入数据库）。").arg(suppliers_.size())
    );
    syncWizardSupplierCombo();
    updateBranchAccess();
}

// 第二步供应商增/删/改后，重建第三步的供应商下拉并尽量保持原选中项。
void CatalogWidget::syncWizardSupplierCombo() {
    if (wizardStep_ != 3 || priceSupplierCombo_ == nullptr) {
        return;
    }
    const auto previousName =
        priceSupplierCombo_->currentIndex() >= 0
            ? priceSupplierCombo_->currentText()
            : QString();
    priceSupplierCombo_->blockSignals(true);
    priceSupplierCombo_->clear();
    const auto suppliers =
        bundleDraft_.value(QStringLiteral("suppliers")).toArray();
    int targetIndex = -1;
    for (int index = 0; index < suppliers.size(); ++index) {
        const auto supplier = suppliers.at(index).toObject()
                                  .value(QStringLiteral("supplier")).toObject();
        priceSupplierCombo_->addItem(
            supplier.value(QStringLiteral("supplierName")).toString()
        );
        if (!previousName.isEmpty() &&
            supplier.value(QStringLiteral("supplierName")).toString() == previousName) {
            targetIndex = index;
        }
    }
    if (targetIndex >= 0) {
        priceSupplierCombo_->setCurrentIndex(targetIndex);
    } else if (priceSupplierCombo_->count() > 0) {
        priceSupplierCombo_->setCurrentIndex(0);
    }
    priceSupplierCombo_->blockSignals(false);
    renderWizardPrices();
}

void CatalogWidget::toggleSelectedSupplier() {
    const auto row = selectedSupplierRow();
    if (!canWrite() || !branchesReady() || row < 0 || supplierBusy_) {
        return;
    }
    const auto supplier = suppliers_.at(row);
    const auto enabled = supplier.value(QStringLiteral("isEnabled")).toBool();
    const QJsonObject body{
        {QStringLiteral("revision"), supplier.value(QStringLiteral("revision")).toInteger()},
        {QStringLiteral("isEnabled"), !enabled},
    };
    setSupplierBusy(true);
    suppliersStatusLabel_->setText(
        enabled ? QStringLiteral("正在停用供应商…") : QStringLiteral("正在启用供应商…")
    );
    apiClient_->patch(
        QStringLiteral("/api/v1/materials/%1/suppliers/%2/enabled")
            .arg(editingMaterialId_)
            .arg(supplier.value(QStringLiteral("id")).toInteger()),
        body,
        [self = QPointer<CatalogWidget>(this)](ApiResponse response) {
            if (!self) {
                return;
            }
            self->setSupplierBusy(false);
            if (!response.succeeded()) {
                self->suppliersStatusLabel_->setText(self->errorText(response));
                return;
            }
            self->loadSuppliers();
        }
    );
}

void CatalogWidget::loadPrices() {
    const auto row = selectedSupplierRow();
    if (!branchesReady() || row < 0 || priceBusy_ || supplierBusy_) {
        return;
    }
    const auto supplier = suppliers_.at(row);
    const auto supplierId = supplier.value(QStringLiteral("id")).toInteger();
    setPriceBusy(true);
    pricesStatusLabel_->setText(QStringLiteral("正在加载价格…"));
    const auto path = QStringLiteral("/api/v1/suppliers/%1/prices").arg(supplierId);
    apiClient_->get(path, [self = QPointer<CatalogWidget>(this)](ApiResponse response) {
        if (self) {
            self->showPrices(response);
        }
    });
}

void CatalogWidget::showPrices(const ApiResponse& response) {
    setPriceBusy(false);
    if (!branchesReady()) {
        return;
    }
    if (!response.succeeded()) {
        pricesStatusLabel_->setText(errorText(response));
        updateBranchAccess();
        return;
    }
    const auto items = response.body.value(QStringLiteral("items"));
    if (!items.isArray()) {
        pricesStatusLabel_->setText(QStringLiteral("服务器返回的价格列表格式不正确。"));
        updateBranchAccess();
        return;
    }

    prices_.clear();
    pricesTable_->setRowCount(0);
    const auto copperBased = materialCopperCheck_->isChecked();
    QJsonArray draftPrices;
    for (const auto& value : items.toArray()) {
        if (!value.isObject()) {
            continue;
        }
        const auto price = value.toObject();
        const auto row = pricesTable_->rowCount();
        prices_.append(price);
        pricesTable_->insertRow(row);
        const auto copper = price.value(QStringLiteral("copperPriceCents"));
        const auto copperText =
            copperBased && !copper.isNull()
                ? priceText(copper.toInteger())
                : QStringLiteral("—");
        pricesTable_->setItem(row, 0, readOnlyItem(copperText));
        pricesTable_->setItem(row, 1, readOnlyItem(priceText(price.value(QStringLiteral("unitPriceCents")).toInteger())));
        pricesTable_->setItem(row, 2, readOnlyItem(price.value(QStringLiteral("isDefault")).toBool() ? QStringLiteral("是") : QStringLiteral("否")));
        pricesTable_->setItem(row, 3, readOnlyItem(price.value(QStringLiteral("isEnabled")).toBool() ? QStringLiteral("启用") : QStringLiteral("停用")));
        pricesTable_->setItem(row, 4, readOnlyItem(QString::number(price.value(QStringLiteral("revision")).toInteger())));
        pricesTable_->setItem(row, 5, readOnlyItem(copperBased ? QStringLiteral("按铜价") : QStringLiteral("普通")));
        draftPrices.append(price);
    }
    // 向导编辑起点：把已加载价格写回 bundleDraft_ 当前供应商条目。
    if (wizardStep_ == 1 || (wizardStep_ == 3 && editingMaterialId_ > 0)) {
        const auto supplierRow = selectedSupplierRow();
        auto draftSuppliers = bundleDraft_.value(QStringLiteral("suppliers")).toArray();
        if (supplierRow >= 0 && supplierRow < draftSuppliers.size()) {
            auto entry = draftSuppliers.at(supplierRow).toObject();
            entry.insert(QStringLiteral("prices"), draftPrices);
            draftSuppliers.replace(supplierRow, entry);
            bundleDraft_.insert(QStringLiteral("suppliers"), draftSuppliers);
        }
    }
    pricesStatusLabel_->setText(QStringLiteral("该供应商共 %1 条价格。")
                                    .arg(prices_.size()));
    updateBranchAccess();
}

void CatalogWidget::beginNewPrice() {
    if (!canWrite()) {
        return;
    }
    if (wizardStep_ == 3) {
        // 向导第 3 步：供应商由下拉框选择。
        if (priceSupplierCombo_->currentIndex() < 0) {
            return;
        }
    } else if (selectedSupplierRow() < 0 || !branchesReady()) {
        return;
    }
    editingPriceId_ = 0;
    editingPriceRevision_ = 0;
    priceEditing_ = true;
    priceCopperEdit_->clear();
    priceUnitEdit_->clear();
    priceDefaultCheck_->setChecked(false);
    priceEditor_->setTitle(QStringLiteral("新增价格"));
    updateBranchAccess();
    applyCopperVisibility();
    if (materialCopperCheck_->isChecked()) {
        priceCopperEdit_->setFocus();
    } else {
        priceUnitEdit_->setFocus();
    }
}

void CatalogWidget::beginEditPrice() {
    const auto row = selectedPriceRow();
    if (!canWrite() || !branchesReady() || row < 0) {
        return;
    }
    const auto price = prices_.at(row);
    editingPriceId_ = price.value(QStringLiteral("id")).toInteger();
    editingPriceRevision_ = price.value(QStringLiteral("revision")).toInteger();
    priceEditing_ = true;
    const auto copper = price.value(QStringLiteral("copperPriceCents"));
    if (materialCopperCheck_->isChecked() && !copper.isNull()) {
        priceCopperEdit_->setText(priceText(copper.toInteger()));
    } else {
        priceCopperEdit_->clear();
    }
    priceUnitEdit_->setText(priceText(price.value(QStringLiteral("unitPriceCents")).toInteger()));
    priceDefaultCheck_->setChecked(price.value(QStringLiteral("isDefault")).toBool());
    priceEditor_->setTitle(QStringLiteral("编辑价格"));
    updateBranchAccess();
    applyCopperVisibility();
}

void CatalogWidget::savePrice() {
    if (!canWrite() || !priceEditing_ || priceBusy_) {
        return;
    }
    // 向导第 3 步的供应商索引来自下拉框；编辑模式来自供应商表格选中行。
    int supplierIndex = -1;
    if (wizardStep_ == 3) {
        supplierIndex = priceSupplierCombo_->currentIndex();
    } else {
        supplierIndex = selectedSupplierRow();
    }
    if (supplierIndex < 0) {
        return;
    }
    const auto supplierId =
        suppliers_.at(supplierIndex).value(QStringLiteral("id")).toInteger();

    QJsonObject body;
    if (materialCopperCheck_->isChecked()) {
        const auto copper = parsePrice(priceCopperEdit_->text());
        if (!copper.has_value()) {
            pricesStatusLabel_->setText(
                QStringLiteral("电线类物料请填写铜价，例如 7.20。")
            );
            return;
        }
        body.insert(QStringLiteral("copperPriceCents"), *copper);
    } else {
        body.insert(QStringLiteral("copperPriceCents"), QJsonValue::Null);
    }
    const auto cents = parsePrice(priceUnitEdit_->text());
    if (!cents.has_value()) {
        pricesStatusLabel_->setText(
            QStringLiteral("单价请输入非负金额，最多保留两位小数，例如 12.50。")
        );
        return;
    }
    body.insert(QStringLiteral("unitPriceCents"), *cents);
    body.insert(QStringLiteral("isDefault"), priceDefaultCheck_->isChecked());
    body.insert(QStringLiteral("isEnabled"), true);

    // 三级向导第 3 步：价格暂存到当前选中供应商草稿的 prices 数组。
    if (wizardStep_ == 3) {
        auto suppliers = bundleDraft_.value(QStringLiteral("suppliers")).toArray();
        if (supplierIndex >= suppliers.size()) {
            return;
        }
        auto entry = suppliers.at(supplierIndex).toObject();
        auto prices = entry.value(QStringLiteral("prices")).toArray();
        // 编辑模式：修改已有价格时更新原条目（保留 id/revision）。
        const auto editingExistingPrice = editingPriceId_ > 0;
        if (editingExistingPrice) {
            for (int index = 0; index < prices.size(); ++index) {
                if (prices.at(index).toObject()
                        .value(QStringLiteral("id")).toInteger() == editingPriceId_) {
                    auto updated = prices.at(index).toObject();
                    for (auto it = body.constBegin(); it != body.constEnd(); ++it) {
                        updated.insert(it.key(), it.value());
                    }
                    prices.replace(index, updated);
                    entry.insert(QStringLiteral("prices"), prices);
                    suppliers.replace(supplierIndex, entry);
                    bundleDraft_.insert(QStringLiteral("suppliers"), suppliers);
                    cancelPriceEdit();
                    renderWizardPrices();
                    pricesStatusLabel_->setText(
                        QStringLiteral("价格已更新（暂存，尚未写入数据库）。")
                    );
                    return;
                }
            }
        }
        prices.append(body);
        entry.insert(QStringLiteral("prices"), prices);
        suppliers.replace(supplierIndex, entry);
        bundleDraft_.insert(QStringLiteral("suppliers"), suppliers);
        cancelPriceEdit();
        renderWizardPrices();
        pricesStatusLabel_->setText(
            QStringLiteral("价格已暂存，可继续为同一供应商添加不同铜价的价格。")
        );
        return;
    }

    const auto updating = editingPriceId_ > 0;
    if (updating) {
        body.insert(QStringLiteral("revision"), editingPriceRevision_);
    }
    setPriceBusy(true);
    pricesStatusLabel_->setText(QStringLiteral("正在保存价格…"));
    const auto callback = [self = QPointer<CatalogWidget>(this), updating](ApiResponse response) {
        if (!self) {
            return;
        }
        self->setPriceBusy(false);
        if (!response.succeeded()) {
            self->pricesStatusLabel_->setText(self->errorText(response));
            return;
        }
        self->cancelPriceEdit();
        self->pricesStatusLabel_->setText(
            updating ? QStringLiteral("价格修改成功。")
                     : QStringLiteral(
                           "价格新增成功，可继续为同一供应商新建不同铜价的价格。"
                       )
        );
        self->loadPrices();
    };
    const auto path = updating
                          ? QStringLiteral("/api/v1/suppliers/%1/prices/%2")
                                .arg(supplierId)
                                .arg(editingPriceId_)
                          : QStringLiteral("/api/v1/suppliers/%1/prices")
                                .arg(supplierId);
    if (updating) {
        apiClient_->put(path, body, callback);
    } else {
        apiClient_->post(path, body, callback);
    }
}

// 向导第 3 步：把当前下拉选中供应商暂存的价格渲染到表格（尚未落库）。
void CatalogWidget::renderWizardPrices() {
    prices_.clear();
    pricesTable_->setRowCount(0);
    const auto suppliers =
        bundleDraft_.value(QStringLiteral("suppliers")).toArray();
    if (wizardStep_ != 3) {
        updateBranchAccess();
        return;
    }
    const auto row = priceSupplierCombo_->currentIndex();
    if (row < 0 || row >= suppliers.size()) {
        updateBranchAccess();
        return;
    }
    const auto prices =
        suppliers.at(row).toObject().value(QStringLiteral("prices")).toArray();
    for (const auto& value : prices) {
        const auto price = value.toObject();
        const auto tableRow = pricesTable_->rowCount();
        prices_.append(price);
        pricesTable_->insertRow(tableRow);
        const auto copper = price.value(QStringLiteral("copperPriceCents"));
        pricesTable_->setItem(tableRow, 0, readOnlyItem(
            !copper.isNull() ? priceText(copper.toInteger()) : QStringLiteral("-")));
        pricesTable_->setItem(tableRow, 1, readOnlyItem(priceText(price.value(QStringLiteral("unitPriceCents")).toInteger())));
        pricesTable_->setItem(tableRow, 2, readOnlyItem(price.value(QStringLiteral("isDefault")).toBool() ? QStringLiteral("是") : QStringLiteral("否")));
        pricesTable_->setItem(tableRow, 3, readOnlyItem(QStringLiteral("暂存")));
        pricesTable_->setItem(tableRow, 4, readOnlyItem(QStringLiteral("-")));
        pricesTable_->setItem(tableRow, 5, readOnlyItem(QStringLiteral("")));
    }
    pricesStatusLabel_->setText(
        QStringLiteral("当前供应商已暂存 %1 条价格。").arg(prices_.size())
    );
    updateBranchAccess();
}

void CatalogWidget::cancelPriceEdit() {
    priceEditing_ = false;
    editingPriceId_ = 0;
    editingPriceRevision_ = 0;
    priceEditor_->setTitle(QStringLiteral("价格信息"));
    updateBranchAccess();
}

void CatalogWidget::toggleSelectedPrice() {
    const auto row = selectedPriceRow();
    if (!canWrite() || !branchesReady() || row < 0 || priceBusy_) {
        return;
    }
    const auto supplierRow = selectedSupplierRow();
    if (supplierRow < 0) {
        return;
    }
    const auto supplierId =
        suppliers_.at(supplierRow).value(QStringLiteral("id")).toInteger();
    const auto price = prices_.at(row);
    const auto enabled = price.value(QStringLiteral("isEnabled")).toBool();
    const QJsonObject body{
        {QStringLiteral("revision"), price.value(QStringLiteral("revision")).toInteger()},
        {QStringLiteral("isEnabled"), !enabled},
    };
    setPriceBusy(true);
    pricesStatusLabel_->setText(
        enabled ? QStringLiteral("正在停用价格…") : QStringLiteral("正在启用价格…")
    );
    apiClient_->patch(
        QStringLiteral("/api/v1/suppliers/%1/prices/%2/enabled")
            .arg(supplierId)
            .arg(price.value(QStringLiteral("id")).toInteger()),
        body,
        [self = QPointer<CatalogWidget>(this)](ApiResponse response) {
            if (!self) {
                return;
            }
            self->setPriceBusy(false);
            if (!response.succeeded()) {
                self->pricesStatusLabel_->setText(self->errorText(response));
                return;
            }
            self->loadPrices();
        }
    );
}

void CatalogWidget::beginNewCustomer() {
    if (!canWrite()) {
        return;
    }
    editingCustomerId_ = 0;
    editingCustomerRevision_ = 0;
    customerEditing_ = true;
    customerNameEdit_->clear();
    customerContactEdit_->clear();
    customerPhoneEdit_->clear();
    customerAddressEdit_->clear();
    customerNotesEdit_->clear();
    customerEditor_->setTitle(QStringLiteral("新增客户"));
    updateWriteAccess();
    customerNameEdit_->setFocus();
}

void CatalogWidget::beginEditCustomer() {
    const auto row = selectedCustomerRow();
    if (!canWrite() || row < 0) {
        return;
    }
    const auto customer = customers_.at(row);
    editingCustomerId_ = customer.value(QStringLiteral("id")).toInteger();
    editingCustomerRevision_ = customer.value(QStringLiteral("revision")).toInteger();
    customerEditing_ = true;
    customerNameEdit_->setText(customer.value(QStringLiteral("name")).toString());
    customerContactEdit_->setText(customer.value(QStringLiteral("contactName")).toString());
    customerPhoneEdit_->setText(customer.value(QStringLiteral("phone")).toString());
    customerAddressEdit_->setText(customer.value(QStringLiteral("address")).toString());
    customerNotesEdit_->setText(customer.value(QStringLiteral("notes")).toString());
    customerEditor_->setTitle(QStringLiteral("编辑客户"));
    updateWriteAccess();
}

void CatalogWidget::saveCustomer() {
    if (!canWrite() || !customerEditing_ || customerBusy_) {
        return;
    }
    const auto name = customerNameEdit_->text().trimmed();
    if (name.isEmpty()) {
        customersStatusLabel_->setText(QStringLiteral("请填写客户名称。"));
        return;
    }
    QJsonObject body{
        {QStringLiteral("name"), name},
        {QStringLiteral("contactName"), customerContactEdit_->text().trimmed()},
        {QStringLiteral("phone"), customerPhoneEdit_->text().trimmed()},
        {QStringLiteral("address"), customerAddressEdit_->text().trimmed()},
        {QStringLiteral("notes"), customerNotesEdit_->text().trimmed()},
    };
    const auto updating = editingCustomerId_ > 0;
    if (updating) {
        body.insert(QStringLiteral("revision"), editingCustomerRevision_);
    }
    setCustomerBusy(true);
    customersStatusLabel_->setText(QStringLiteral("正在保存客户…"));
    const auto callback = [self = QPointer<CatalogWidget>(this), updating](ApiResponse response) {
        if (!self) {
            return;
        }
        self->setCustomerBusy(false);
        if (!response.succeeded()) {
            self->customersStatusLabel_->setText(self->errorText(response));
            return;
        }
        self->cancelCustomerEdit();
        self->customersStatusLabel_->setText(
            updating ? QStringLiteral("客户修改成功。") : QStringLiteral("客户新增成功。")
        );
        self->loadCustomers();
    };
    if (updating) {
        apiClient_->put(
            QStringLiteral("/api/v1/customers/%1").arg(editingCustomerId_),
            body,
            callback
        );
    } else {
        apiClient_->post(QStringLiteral("/api/v1/customers"), body, callback);
    }
}

void CatalogWidget::cancelCustomerEdit() {
    customerEditing_ = false;
    editingCustomerId_ = 0;
    editingCustomerRevision_ = 0;
    customerEditor_->setTitle(QStringLiteral("客户信息"));
    updateWriteAccess();
}

int CatalogWidget::selectedMaterialRow() const {
    const auto rows = materialsTable_->selectionModel()->selectedRows();
    if (rows.isEmpty() || rows.first().row() >= materials_.size()) {
        return -1;
    }
    return rows.first().row();
}

int CatalogWidget::selectedSupplierRow() const {
    const auto rows = suppliersTable_->selectionModel()->selectedRows();
    if (rows.isEmpty() || rows.first().row() >= suppliers_.size()) {
        return -1;
    }
    return rows.first().row();
}

int CatalogWidget::selectedPriceRow() const {
    const auto rows = pricesTable_->selectionModel()->selectedRows();
    if (rows.isEmpty() || rows.first().row() >= prices_.size()) {
        return -1;
    }
    return rows.first().row();
}

int CatalogWidget::selectedCustomerRow() const {
    const auto rows = customersTable_->selectionModel()->selectedRows();
    if (rows.isEmpty() || rows.first().row() >= customers_.size()) {
        return -1;
    }
    return rows.first().row();
}

QString CatalogWidget::errorText(const ApiResponse& response) const {
    if (response.error.kind == ApiErrorKind::Network) {
        return QStringLiteral("网络连接失败，请确认本地服务已启动。%1")
            .arg(response.error.message.isEmpty()
                     ? QString()
                     : QStringLiteral("（%1）").arg(response.error.message));
    }
    if (response.httpStatus == 401) {
        return QStringLiteral("登录已失效，请重新登录。");
    }
    if (response.httpStatus == 403) {
        return QStringLiteral("当前账号没有执行此操作的权限。");
    }
    if (response.error.code == QStringLiteral("revision_conflict")) {
        return QStringLiteral("保存失败：数据已被其他操作修改，请刷新后重试。");
    }
    if (response.error.code == QStringLiteral("duplicate_code")) {
        return QStringLiteral("保存失败：物料编码已存在。");
    }
    if (response.error.code == QStringLiteral("invalid_request") ||
        response.error.code == QStringLiteral("invalid_json")) {
        // 透传服务端的字段级 message，便于精确定位失败原因。
        if (!response.error.message.isEmpty()) {
            return QStringLiteral("提交内容不符合要求：%1")
                .arg(response.error.message);
        }
        return QStringLiteral("提交内容不符合要求，请检查后重试。");
    }
    return QStringLiteral("操作失败（%1）：%2")
        .arg(
            response.error.code.isEmpty() ? QStringLiteral("unknown")
                                          : response.error.code,
            response.error.message.isEmpty() ? QStringLiteral("未知错误")
                                             : response.error.message
        );
}

void CatalogWidget::setMaterialBusy(bool busy) {
    materialBusy_ = busy;
    const auto ready = sessionReady();
    materialsSearchEdit_->setEnabled(ready && !busy);
    materialsSearchButton_->setEnabled(ready && !busy);
    materialsRefreshButton_->setEnabled(ready && !busy);
    updateMaterialPaging();
    updateWriteAccess();
}

void CatalogWidget::setCustomerBusy(bool busy) {
    customerBusy_ = busy;
    const auto ready = sessionReady();
    customersSearchEdit_->setEnabled(ready && !busy);
    customersSearchButton_->setEnabled(ready && !busy);
    customersRefreshButton_->setEnabled(ready && !busy);
    updateCustomerPaging();
    updateWriteAccess();
}

void CatalogWidget::setSupplierBusy(bool busy) {
    supplierBusy_ = busy;
    updateBranchAccess();
}

void CatalogWidget::setPriceBusy(bool busy) {
    priceBusy_ = busy;
    updateBranchAccess();
}

} // namespace manage::desktop
