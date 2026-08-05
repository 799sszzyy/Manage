#include "manage/desktop/excel_tools_widget.h"

#include "manage/desktop/api_client.h"

#include "manage/excel/workbook_service.h"

#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <functional>
#include <utility>

namespace manage::desktop {
namespace {

QString xlsxPath(QString path) {
    if (!path.isEmpty() && !path.endsWith(QStringLiteral(".xlsx"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".xlsx");
    }
    return path;
}

manage::data::Material materialFromJson(const QJsonObject& object) {
    manage::data::Material material;
    material.id = object.value(QStringLiteral("id")).toInteger();
    material.code = object.value(QStringLiteral("code")).toString();
    material.name = object.value(QStringLiteral("name")).toString();
    material.specification = object.value(QStringLiteral("specification")).toString();
    material.unit = object.value(QStringLiteral("unit")).toString();
    material.category = object.value(QStringLiteral("category")).toString();
    material.currentUnitPriceCents = object.value(QStringLiteral("currentUnitPriceCents")).toInteger();
    material.isEnabled = object.value(QStringLiteral("isEnabled")).toBool(true);
    return material;
}

manage::data::BomTemplate bomFromJson(const QJsonObject& object) {
    manage::data::BomTemplate bom;
    bom.summary.id = object.value(QStringLiteral("id")).toInteger();
    bom.summary.code = object.value(QStringLiteral("code")).toString();
    bom.summary.name = object.value(QStringLiteral("name")).toString();
    bom.summary.description = object.value(QStringLiteral("description")).toString();
    bom.summary.isEnabled = object.value(QStringLiteral("isEnabled")).toBool(true);
    bom.summary.revision = object.value(QStringLiteral("revision")).toInt(1);
    for (const auto& value : object.value(QStringLiteral("items")).toArray()) {
        const auto itemObject = value.toObject();
        manage::data::BomItem item;
        item.id = itemObject.value(QStringLiteral("id")).toInteger();
        item.lineNo = itemObject.value(QStringLiteral("lineNo")).toInt();
        item.materialId = itemObject.value(QStringLiteral("materialId")).toInteger();
        item.materialCode = itemObject.value(QStringLiteral("materialCode")).toString();
        item.materialName = itemObject.value(QStringLiteral("materialName")).toString();
        item.materialSpecification = itemObject.value(QStringLiteral("materialSpecification")).toString();
        item.materialUnit = itemObject.value(QStringLiteral("materialUnit")).toString();
        item.quantityMicros = itemObject.value(QStringLiteral("quantityMicros")).toInteger();
        item.notes = itemObject.value(QStringLiteral("notes")).toString();
        bom.items.push_back(std::move(item));
    }
    return bom;
}

manage::data::QuoteDocument quoteFromJson(const QJsonObject& object) {
    manage::data::QuoteDocument quote;
    quote.summary.id = object.value(QStringLiteral("id")).toInteger();
    quote.summary.quoteNumber = object.value(QStringLiteral("quoteNumber")).toString();
    quote.summary.customerName = object.value(QStringLiteral("customerName")).toString();
    quote.summary.status = manage::data::quoteStatusFromCode(
        object.value(QStringLiteral("status")).toString()
    ).value_or(manage::data::QuoteStatus::Draft);
    quote.customerContact = object.value(QStringLiteral("customerContact")).toString();
    quote.customerPhone = object.value(QStringLiteral("customerPhone")).toString();
    quote.customerAddress = object.value(QStringLiteral("customerAddress")).toString();
    quote.materialCostCents = object.value(QStringLiteral("materialCostCents")).toInteger();
    quote.freightCents = object.value(QStringLiteral("freightCents")).toInteger();
    quote.otherFeesCents = object.value(QStringLiteral("otherFeesCents")).toInteger();
    quote.priceWithTaxCents = object.value(QStringLiteral("priceWithTaxCents")).toInteger();
    for (const auto& value : object.value(QStringLiteral("items")).toArray()) {
        const auto itemObject = value.toObject();
        manage::data::QuoteItemSnapshot item;
        item.id = itemObject.value(QStringLiteral("id")).toInteger();
        item.lineNo = itemObject.value(QStringLiteral("lineNo")).toInt();
        item.materialId = itemObject.value(QStringLiteral("materialId")).toInteger();
        item.materialCode = itemObject.value(QStringLiteral("materialCode")).toString();
        item.materialName = itemObject.value(QStringLiteral("materialName")).toString();
        item.specification = itemObject.value(QStringLiteral("specification")).toString();
        item.unit = itemObject.value(QStringLiteral("unit")).toString();
        item.quantityMicros = itemObject.value(QStringLiteral("quantityMicros")).toInteger();
        item.unitPriceCents = itemObject.value(QStringLiteral("unitPriceCents")).toInteger();
        item.subtotalCents = itemObject.value(QStringLiteral("subtotalCents")).toInteger();
        item.notes = itemObject.value(QStringLiteral("notes")).toString();
        quote.items.push_back(std::move(item));
    }
    return quote;
}

bool saveWorkbook(
    const QString& path,
    const std::function<bool(QIODevice*, QString*)>& writer,
    QString& error
) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        error = file.errorString();
        return false;
    }
    const auto written = writer(&file, &error);
    file.close();
    return written;
}

} // namespace

ExcelToolsWidget::ExcelToolsWidget(ApiClient* apiClient, QWidget* parent)
    : QWidget(parent), apiClient_(apiClient) {
    setObjectName(QStringLiteral("excelToolsWidget"));
    auto* layout = new QVBoxLayout(this);
    auto* explanation = new QLabel(
        QStringLiteral("生成真实 XLSX 文件；物料导入会先完整校验，确认后再用一笔事务写入，不会只导入一半。"),
        this
    );
    explanation->setWordWrap(true);
    layout->addWidget(explanation);

    auto* materialGroup = new QGroupBox(QStringLiteral("物料 Excel"), this);
    auto* materialLayout = new QHBoxLayout(materialGroup);
    templateButton_ = new QPushButton(QStringLiteral("下载导入模板"), materialGroup);
    importButton_ = new QPushButton(QStringLiteral("批量导入物料"), materialGroup);
    materialsButton_ = new QPushButton(QStringLiteral("导出全部物料"), materialGroup);
    templateButton_->setObjectName(QStringLiteral("excelTemplateButton"));
    importButton_->setObjectName(QStringLiteral("excelImportButton"));
    materialsButton_->setObjectName(QStringLiteral("excelMaterialsButton"));
    materialLayout->addWidget(templateButton_);
    materialLayout->addWidget(importButton_);
    materialLayout->addWidget(materialsButton_);
    layout->addWidget(materialGroup);

    auto* businessGroup = new QGroupBox(QStringLiteral("BOM / 报价单 Excel"), this);
    auto* businessLayout = new QFormLayout(businessGroup);
    auto* bomRow = new QWidget(businessGroup);
    auto* bomLayout = new QHBoxLayout(bomRow);
    bomLayout->setContentsMargins(0, 0, 0, 0);
    bomIdSpin_ = new QSpinBox(bomRow);
    bomIdSpin_->setRange(1, 1'000'000'000);
    bomButton_ = new QPushButton(QStringLiteral("导出 BOM"), bomRow);
    bomButton_->setObjectName(QStringLiteral("excelBomButton"));
    bomLayout->addWidget(bomIdSpin_);
    bomLayout->addWidget(bomButton_);
    businessLayout->addRow(QStringLiteral("BOM ID"), bomRow);
    auto* quoteRow = new QWidget(businessGroup);
    auto* quoteLayout = new QHBoxLayout(quoteRow);
    quoteLayout->setContentsMargins(0, 0, 0, 0);
    quoteIdSpin_ = new QSpinBox(quoteRow);
    quoteIdSpin_->setRange(1, 1'000'000'000);
    quoteButton_ = new QPushButton(QStringLiteral("导出报价单"), quoteRow);
    quoteButton_->setObjectName(QStringLiteral("excelQuoteButton"));
    quoteLayout->addWidget(quoteIdSpin_);
    quoteLayout->addWidget(quoteButton_);
    businessLayout->addRow(QStringLiteral("报价单 ID"), quoteRow);
    layout->addWidget(businessGroup);

    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName(QStringLiteral("excelStatusLabel"));
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);
    layout->addStretch();

    connect(templateButton_, &QPushButton::clicked, this, [this]() { saveMaterialTemplate(); });
    connect(importButton_, &QPushButton::clicked, this, [this]() { importMaterials(); });
    connect(materialsButton_, &QPushButton::clicked, this, [this]() { exportMaterials(); });
    connect(bomButton_, &QPushButton::clicked, this, [this]() { exportBom(); });
    connect(quoteButton_, &QPushButton::clicked, this, [this]() { exportQuote(); });
    if (apiClient_ != nullptr) {
        connect(apiClient_, &ApiClient::sessionChanged, this, [this](bool) { updatePermissions(); });
    }
    updatePermissions();
}

void ExcelToolsWidget::saveMaterialTemplate() {
    const auto path = xlsxPath(QFileDialog::getSaveFileName(
        this, QStringLiteral("保存物料导入模板"), QStringLiteral("物料导入模板.xlsx"),
        QStringLiteral("Excel 工作簿 (*.xlsx)")));
    if (path.isEmpty()) {
        return;
    }
    QString error;
    const auto ok = saveWorkbook(path, [](QIODevice* output, QString* failure) {
        return manage::excel::WorkbookService::exportMaterialTemplate(output, failure);
    }, error);
    statusLabel_->setText(ok ? QStringLiteral("模板已保存：%1").arg(path)
                             : QStringLiteral("保存模板失败：%1").arg(error));
}

void ExcelToolsWidget::importMaterials() {
    const auto path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择物料工作簿"), {}, QStringLiteral("Excel 工作簿 (*.xlsx)"));
    if (!path.isEmpty()) {
        submitMaterialImport(path, true);
    }
}

void ExcelToolsWidget::submitMaterialImport(const QString& path, bool validateOnly) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        statusLabel_->setText(QStringLiteral("无法打开工作簿：%1").arg(file.errorString()));
        return;
    }
    const auto parsed = manage::excel::WorkbookService::importMaterials(&file);
    if (!parsed.ok()) {
        QStringList errors;
        for (const auto& item : parsed.errors) {
            errors.append(QStringLiteral("第 %1 行 %2：%3").arg(item.row).arg(item.field, item.message));
            if (errors.size() >= 10) {
                break;
            }
        }
        statusLabel_->setText(QStringLiteral("工作簿校验失败：\n%1").arg(errors.join(QStringLiteral("\n"))));
        return;
    }
    QJsonArray rows;
    for (const auto& imported : parsed.rows) {
        const auto& material = imported.material;
        rows.append(QJsonObject{
            {QStringLiteral("sourceRow"), imported.sourceRow},
            {QStringLiteral("code"), material.code},
            {QStringLiteral("name"), material.name},
            {QStringLiteral("specification"), material.specification},
            {QStringLiteral("unit"), material.unit},
            {QStringLiteral("category"), material.category},
            {QStringLiteral("currentUnitPriceCents"), material.currentUnitPriceCents},
            {QStringLiteral("isEnabled"), material.isEnabled},
        });
    }
    setBusy(true, validateOnly ? QStringLiteral("正在向服务器校验整批物料……")
                               : QStringLiteral("正在用一笔事务导入整批物料……"));
    apiClient_->post(
        QStringLiteral("/api/v1/materials/import"),
        QJsonObject{{QStringLiteral("validateOnly"), validateOnly}, {QStringLiteral("rows"), rows}},
        [this, path, validateOnly](ApiResponse response) {
            setBusy(false);
            if (!response.succeeded()) {
                showApiError(QStringLiteral("批量导入"), response);
                return;
            }
            const auto creates = response.body.value(QStringLiteral("createCount")).toInt();
            const auto updates = response.body.value(QStringLiteral("updateCount")).toInt();
            if (validateOnly) {
                const auto answer = QMessageBox::question(
                    this,
                    QStringLiteral("确认导入"),
                    QStringLiteral("校验通过：将新增 %1 条、更新 %2 条。是否写入数据库？")
                        .arg(creates).arg(updates)
                );
                if (answer == QMessageBox::Yes) {
                    submitMaterialImport(path, false);
                } else {
                    statusLabel_->setText(QStringLiteral("仅完成校验，数据库未发生变化。"));
                }
                return;
            }
            statusLabel_->setText(QStringLiteral("导入完成：新增 %1 条、更新 %2 条，整批事务已提交。")
                                      .arg(creates).arg(updates));
        }
    );
}

void ExcelToolsWidget::exportMaterials() {
    const auto path = xlsxPath(QFileDialog::getSaveFileName(
        this, QStringLiteral("导出全部物料"), QStringLiteral("物料清单.xlsx"),
        QStringLiteral("Excel 工作簿 (*.xlsx)")));
    if (!path.isEmpty()) {
        setBusy(true, QStringLiteral("正在读取全部物料……"));
        fetchMaterialPage(1, path, {});
    }
}

void ExcelToolsWidget::fetchMaterialPage(
    int page,
    const QString& outputPath,
    std::vector<manage::data::Material> materials
) {
    apiClient_->get(
        QStringLiteral("/api/v1/materials?page=%1&pageSize=100").arg(page),
        [this, page, outputPath, materials = std::move(materials)](ApiResponse response) mutable {
            if (!response.succeeded()) {
                setBusy(false);
                showApiError(QStringLiteral("导出物料"), response);
                return;
            }
            for (const auto& value : response.body.value(QStringLiteral("items")).toArray()) {
                materials.push_back(materialFromJson(value.toObject()));
            }
            const auto totalPages = response.body.value(QStringLiteral("totalPages")).toInt();
            if (page < totalPages) {
                fetchMaterialPage(page + 1, outputPath, std::move(materials));
                return;
            }
            QString error;
            const auto ok = saveWorkbook(outputPath, [&materials](QIODevice* output, QString* failure) {
                return manage::excel::WorkbookService::exportMaterials(output, materials, failure);
            }, error);
            setBusy(false, ok ? QStringLiteral("物料已导出：%1").arg(outputPath)
                              : QStringLiteral("导出失败：%1").arg(error));
        }
    );
}

void ExcelToolsWidget::exportBom() {
    const auto path = xlsxPath(QFileDialog::getSaveFileName(
        this, QStringLiteral("导出 BOM"), QStringLiteral("BOM-%1.xlsx").arg(bomIdSpin_->value()),
        QStringLiteral("Excel 工作簿 (*.xlsx)")));
    if (path.isEmpty()) {
        return;
    }
    setBusy(true, QStringLiteral("正在读取 BOM……"));
    apiClient_->get(QStringLiteral("/api/v1/boms/%1").arg(bomIdSpin_->value()),
        [this, path](ApiResponse response) {
            if (!response.succeeded()) {
                setBusy(false);
                showApiError(QStringLiteral("导出 BOM"), response);
                return;
            }
            const auto bom = bomFromJson(response.body);
            QString error;
            const auto ok = saveWorkbook(path, [&bom](QIODevice* output, QString* failure) {
                return manage::excel::WorkbookService::exportBom(output, bom, failure);
            }, error);
            setBusy(false, ok ? QStringLiteral("BOM 已导出：%1").arg(path)
                              : QStringLiteral("导出失败：%1").arg(error));
        });
}

void ExcelToolsWidget::exportQuote() {
    const auto path = xlsxPath(QFileDialog::getSaveFileName(
        this, QStringLiteral("导出报价单"), QStringLiteral("报价单-%1.xlsx").arg(quoteIdSpin_->value()),
        QStringLiteral("Excel 工作簿 (*.xlsx)")));
    if (path.isEmpty()) {
        return;
    }
    setBusy(true, QStringLiteral("正在读取报价单……"));
    apiClient_->get(QStringLiteral("/api/v1/quotes/%1").arg(quoteIdSpin_->value()),
        [this, path](ApiResponse response) {
            if (!response.succeeded()) {
                setBusy(false);
                showApiError(QStringLiteral("导出报价单"), response);
                return;
            }
            const auto quote = quoteFromJson(response.body);
            QString error;
            const auto ok = saveWorkbook(path, [&quote](QIODevice* output, QString* failure) {
                return manage::excel::WorkbookService::exportQuote(output, quote, failure);
            }, error);
            setBusy(false, ok ? QStringLiteral("报价单已导出：%1").arg(path)
                              : QStringLiteral("导出失败：%1").arg(error));
        });
}

void ExcelToolsWidget::updatePermissions() {
    const auto role = apiClient_ == nullptr
                          ? QString{}
                          : apiClient_->session().user.value(QStringLiteral("role")).toString();
    importButton_->setEnabled(!busy_ && role == QStringLiteral("admin"));
}

void ExcelToolsWidget::setBusy(bool busy, const QString& message) {
    busy_ = busy;
    templateButton_->setEnabled(!busy);
    materialsButton_->setEnabled(!busy);
    bomButton_->setEnabled(!busy);
    quoteButton_->setEnabled(!busy);
    updatePermissions();
    if (!message.isEmpty()) {
        statusLabel_->setText(message);
    }
}

void ExcelToolsWidget::showApiError(const QString& action, const ApiResponse& response) {
    const auto message = response.error.message.isEmpty()
                             ? QStringLiteral("服务器没有返回具体原因")
                             : response.error.message;
    statusLabel_->setText(QStringLiteral("%1失败：%2").arg(action, message));
}

} // namespace manage::desktop
