#include "manage/excel/workbook_service.h"

#include <xlsxdocument.h>
#include <xlsxformat.h>

#include <QColor>
#include <QHash>
#include <QLocale>
#include <QMetaType>
#include <QSet>
#include <QVariant>

#include <cmath>
#include <limits>

namespace manage::excel {
namespace {

using QXlsx::Document;
using QXlsx::Format;

const QStringList kMaterialHeaders{
    QStringLiteral("物料编码"),
    QStringLiteral("物料名称"),
    QStringLiteral("规格"),
    QStringLiteral("单位"),
    QStringLiteral("类别"),
    QStringLiteral("当前单价"),
    QStringLiteral("启用"),
};

Format titleFormat() {
    Format format;
    format.setFontBold(true);
    format.setFontSize(16);
    format.setFontColor(QColor(QStringLiteral("#FFFFFF")));
    format.setPatternBackgroundColor(QColor(QStringLiteral("#17365D")));
    format.setHorizontalAlignment(Format::AlignHCenter);
    format.setVerticalAlignment(Format::AlignVCenter);
    return format;
}

Format headerFormat() {
    Format format;
    format.setFontBold(true);
    format.setFontColor(QColor(QStringLiteral("#FFFFFF")));
    format.setPatternBackgroundColor(QColor(QStringLiteral("#1F4E78")));
    format.setHorizontalAlignment(Format::AlignHCenter);
    format.setVerticalAlignment(Format::AlignVCenter);
    format.setTextWrap(true);
    format.setBorderStyle(Format::BorderThin);
    return format;
}

Format moneyFormat() {
    Format format;
    format.setNumberFormat(QStringLiteral("0.00"));
    return format;
}

Format percentageFormat() {
    Format format;
    format.setNumberFormat(QStringLiteral("0.00%"));
    return format;
}

void setError(QString* error, const QString& value) {
    if (error != nullptr) {
        *error = value;
    }
}

bool save(Document& document, QIODevice* output, QString* error) {
    if (output == nullptr || !output->isWritable()) {
        setError(error, QStringLiteral("输出目标不可写"));
        return false;
    }
    if (!document.saveAs(output)) {
        setError(error, QStringLiteral("无法生成 XLSX 文件"));
        return false;
    }
    setError(error, {});
    return true;
}

void writeHeaders(Document& document, int row, const QStringList& headers) {
    const auto format = headerFormat();
    for (qsizetype column = 0; column < headers.size(); ++column) {
        document.write(row, static_cast<int>(column) + 1, headers.at(column), format);
    }
    document.setRowHeight(row, 28);
}

void configureMaterialColumns(Document& document) {
    document.setColumnWidth(1, 18);
    document.setColumnWidth(2, 24);
    document.setColumnWidth(3, 24);
    document.setColumnWidth(4, 12);
    document.setColumnWidth(5, 18);
    document.setColumnWidth(6, 14);
    document.setColumnWidth(7, 10);
}

void renameFirstSheet(Document& document, const QString& name) {
    const auto names = document.sheetNames();
    if (names.isEmpty()) {
        document.addSheet(name);
    } else {
        document.renameSheet(names.front(), name);
    }
    document.selectSheet(name);
}

QString cellText(const QVariant& value) {
    return value.isValid() ? value.toString().trimmed() : QString{};
}

bool rowIsEmpty(Document& document, int row) {
    for (int column = 1; column <= kMaterialHeaders.size(); ++column) {
        if (!cellText(document.read(row, column)).isEmpty()) {
            return false;
        }
    }
    return true;
}

std::optional<qint64> centsFromCell(const QVariant& value) {
    if (!value.isValid()) {
        return std::nullopt;
    }
    bool ok = false;
    double amount = 0.0;
    const auto type = value.metaType().id();
    if (type == QMetaType::Double || type == QMetaType::Float ||
        type == QMetaType::Int || type == QMetaType::UInt ||
        type == QMetaType::LongLong || type == QMetaType::ULongLong) {
        amount = value.toDouble(&ok);
    } else {
        amount = QLocale::c().toDouble(value.toString().trimmed(), &ok);
    }
    if (!ok || !std::isfinite(amount) || amount < 0.0) {
        return std::nullopt;
    }
    const auto scaled = amount * 100.0;
    if (scaled > static_cast<double>(std::numeric_limits<qint64>::max()) ||
        std::abs(scaled - std::round(scaled)) > 0.000001) {
        return std::nullopt;
    }
    return static_cast<qint64>(std::llround(scaled));
}

std::optional<bool> enabledFromCell(const QVariant& value) {
    if (!value.isValid()) {
        return std::nullopt;
    }
    if (value.metaType().id() == QMetaType::Bool) {
        return value.toBool();
    }
    const auto text = value.toString().trimmed().toLower();
    if (text == QStringLiteral("是") || text == QStringLiteral("true") ||
        text == QStringLiteral("1") || text == QStringLiteral("启用")) {
        return true;
    }
    if (text == QStringLiteral("否") || text == QStringLiteral("false") ||
        text == QStringLiteral("0") || text == QStringLiteral("停用")) {
        return false;
    }
    return std::nullopt;
}

void appendError(
    MaterialImportResult& result,
    int row,
    const QString& field,
    const QString& message
) {
    result.errors.push_back({row, field, message});
}

void writeMoney(Document& document, int row, int column, qint64 cents) {
    document.write(row, column, static_cast<double>(cents) / 100.0, moneyFormat());
}

QString statusName(manage::data::QuoteStatus status) {
    switch (status) {
    case manage::data::QuoteStatus::Draft:
        return QStringLiteral("草稿");
    case manage::data::QuoteStatus::Issued:
        return QStringLiteral("已报价");
    case manage::data::QuoteStatus::Void:
        return QStringLiteral("已作废");
    }
    return QStringLiteral("未知");
}

void writeStatisticsRows(
    Document& document,
    const QString& sheetName,
    const std::vector<StatisticsRow>& rows
) {
    document.addSheet(sheetName);
    writeHeaders(
        document,
        1,
        {QStringLiteral("维度"), QStringLiteral("报价数量"), QStringLiteral("报价金额")}
    );
    int row = 2;
    for (const auto& item : rows) {
        document.write(row, 1, item.dimension);
        document.write(row, 2, item.quoteCount);
        writeMoney(document, row, 3, item.totalCents);
        ++row;
    }
    document.setColumnWidth(1, 28);
    document.setColumnWidth(2, 14);
    document.setColumnWidth(3, 16);
}

} // namespace

bool WorkbookService::exportMaterialTemplate(QIODevice* output, QString* error) {
    Document document;
    document.setDocumentProperty(QStringLiteral("title"), QStringLiteral("物料导入模板"));
    renameFirstSheet(document, QStringLiteral("物料"));
    writeHeaders(document, 1, kMaterialHeaders);
    configureMaterialColumns(document);

    document.write(2, 1, QStringLiteral("MAT-001"));
    document.write(2, 2, QStringLiteral("示例物料（导入前请删除本行）"));
    document.write(2, 3, QStringLiteral("规格型号"));
    document.write(2, 4, QStringLiteral("件"));
    document.write(2, 5, QStringLiteral("示例分类"));
    document.write(2, 6, 12.34, moneyFormat());
    document.write(2, 7, QStringLiteral("是"));

    document.addSheet(QStringLiteral("填写说明"));
    writeHeaders(document, 1, {QStringLiteral("字段"), QStringLiteral("填写要求")});
    const std::vector<std::pair<QString, QString>> instructions{
        {QStringLiteral("物料编码"), QStringLiteral("必填；1-64 位字母、数字、点、下划线或短横线；相同编码会更新")},
        {QStringLiteral("物料名称"), QStringLiteral("必填；最多 200 个字符")},
        {QStringLiteral("规格"), QStringLiteral("可空；最多 500 个字符")},
        {QStringLiteral("单位"), QStringLiteral("必填；最多 32 个字符")},
        {QStringLiteral("类别"), QStringLiteral("可空；最多 100 个字符")},
        {QStringLiteral("当前单价"), QStringLiteral("必填；非负数，最多两位小数")},
        {QStringLiteral("启用"), QStringLiteral("填写 是/否、true/false 或 1/0")},
    };
    int row = 2;
    for (const auto& [field, instruction] : instructions) {
        document.write(row, 1, field);
        document.write(row, 2, instruction);
        ++row;
    }
    document.setColumnWidth(1, 18);
    document.setColumnWidth(2, 72);
    return save(document, output, error);
}

bool WorkbookService::exportMaterials(
    QIODevice* output,
    const std::vector<manage::data::Material>& materials,
    QString* error
) {
    Document document;
    document.setDocumentProperty(QStringLiteral("title"), QStringLiteral("物料清单"));
    renameFirstSheet(document, QStringLiteral("物料"));
    writeHeaders(document, 1, kMaterialHeaders);
    configureMaterialColumns(document);

    int row = 2;
    for (const auto& material : materials) {
        document.write(row, 1, material.code);
        document.write(row, 2, material.name);
        document.write(row, 3, material.specification);
        document.write(row, 4, material.unit);
        document.write(row, 5, material.category);
        writeMoney(document, row, 6, material.currentUnitPriceCents);
        document.write(row, 7, material.isEnabled ? QStringLiteral("是") : QStringLiteral("否"));
        ++row;
    }
    return save(document, output, error);
}

MaterialImportResult WorkbookService::importMaterials(QIODevice* input) {
    MaterialImportResult result;
    if (input == nullptr || !input->isReadable()) {
        appendError(result, 0, QStringLiteral("file"), QStringLiteral("输入文件不可读"));
        return result;
    }

    Document document(input);
    if (!document.load()) {
        appendError(result, 0, QStringLiteral("file"), QStringLiteral("不是可读取的 XLSX 文件"));
        return result;
    }
    if (!document.selectSheet(QStringLiteral("物料"))) {
        if (document.sheetNames().isEmpty() || !document.selectSheet(0)) {
            appendError(result, 0, QStringLiteral("sheet"), QStringLiteral("工作簿中没有工作表"));
            return result;
        }
    }

    for (int column = 1; column <= kMaterialHeaders.size(); ++column) {
        if (cellText(document.read(1, column)) != kMaterialHeaders.at(column - 1)) {
            appendError(
                result,
                1,
                QStringLiteral("header"),
                QStringLiteral("第 %1 列应为“%2”").arg(column).arg(kMaterialHeaders.at(column - 1))
            );
        }
    }
    if (!result.errors.empty()) {
        return result;
    }

    const auto lastRow = document.dimension().lastRow();
    if (lastRow - 1 > maximumMaterialRows) {
        appendError(
            result,
            0,
            QStringLiteral("rows"),
            QStringLiteral("一次最多导入 %1 行物料").arg(maximumMaterialRows)
        );
        return result;
    }

    QSet<QString> seenCodes;
    for (int row = 2; row <= lastRow; ++row) {
        if (rowIsEmpty(document, row)) {
            continue;
        }
        manage::data::MaterialDraft draft;
        draft.code = cellText(document.read(row, 1));
        draft.name = cellText(document.read(row, 2));
        draft.specification = cellText(document.read(row, 3));
        draft.unit = cellText(document.read(row, 4));
        draft.category = cellText(document.read(row, 5));

        if (draft.code.isEmpty()) {
            appendError(result, row, QStringLiteral("code"), QStringLiteral("物料编码不能为空"));
        } else if (seenCodes.contains(draft.code)) {
            appendError(result, row, QStringLiteral("code"), QStringLiteral("文件内物料编码重复"));
        } else {
            seenCodes.insert(draft.code);
        }
        if (draft.name.isEmpty()) {
            appendError(result, row, QStringLiteral("name"), QStringLiteral("物料名称不能为空"));
        }
        if (draft.unit.isEmpty()) {
            appendError(result, row, QStringLiteral("unit"), QStringLiteral("单位不能为空"));
        }

        const auto cents = centsFromCell(document.read(row, 6));
        if (!cents.has_value()) {
            appendError(result, row, QStringLiteral("currentUnitPrice"), QStringLiteral("当前单价应为非负数且最多两位小数"));
        } else {
            draft.currentUnitPriceCents = *cents;
        }
        const auto enabled = enabledFromCell(document.read(row, 7));
        if (!enabled.has_value()) {
            appendError(result, row, QStringLiteral("isEnabled"), QStringLiteral("启用列应填写 是/否、true/false 或 1/0"));
        } else {
            draft.isEnabled = *enabled;
        }
        result.rows.push_back({row, std::move(draft)});
    }
    if (result.rows.empty() && result.errors.empty()) {
        appendError(result, 0, QStringLiteral("rows"), QStringLiteral("没有可导入的物料行"));
    }
    return result;
}

bool WorkbookService::exportBom(
    QIODevice* output,
    const manage::data::BomTemplate& bom,
    QString* error
) {
    Document document;
    renameFirstSheet(document, QStringLiteral("BOM"));
    document.mergeCells(QStringLiteral("A1:G1"), titleFormat());
    document.write(1, 1, QStringLiteral("BOM：%1 - %2").arg(bom.summary.code, bom.summary.name), titleFormat());
    document.write(2, 1, QStringLiteral("说明"), headerFormat());
    document.mergeCells(QStringLiteral("B2:G2"));
    document.write(2, 2, bom.summary.description);
    writeHeaders(document, 4, {
        QStringLiteral("行号"), QStringLiteral("物料编码"), QStringLiteral("物料名称"),
        QStringLiteral("规格"), QStringLiteral("单位"), QStringLiteral("数量"), QStringLiteral("备注")
    });
    int row = 5;
    for (const auto& item : bom.items) {
        document.write(row, 1, item.lineNo);
        document.write(row, 2, item.materialCode);
        document.write(row, 3, item.materialName);
        document.write(row, 4, item.materialSpecification);
        document.write(row, 5, item.materialUnit);
        document.write(row, 6, static_cast<double>(item.quantityMicros) / 1'000'000.0);
        document.write(row, 7, item.notes);
        ++row;
    }
    document.setColumnWidth(1, 10);
    document.setColumnWidth(2, 18);
    document.setColumnWidth(3, 24);
    document.setColumnWidth(4, 24);
    document.setColumnWidth(5, 10);
    document.setColumnWidth(6, 14);
    document.setColumnWidth(7, 28);
    return save(document, output, error);
}

bool WorkbookService::exportQuote(
    QIODevice* output,
    const manage::data::QuoteDocument& quote,
    QString* error
) {
    Document document;
    renameFirstSheet(document, QStringLiteral("报价单"));
    document.mergeCells(QStringLiteral("A1:H1"), titleFormat());
    document.write(1, 1, QStringLiteral("报价单 %1").arg(quote.summary.quoteNumber), titleFormat());
    document.write(2, 1, QStringLiteral("客户"), headerFormat());
    document.write(2, 2, quote.summary.customerName);
    document.write(2, 4, QStringLiteral("状态"), headerFormat());
    document.write(2, 5, statusName(quote.summary.status));
    document.write(3, 1, QStringLiteral("联系人"), headerFormat());
    document.write(3, 2, quote.customerContact);
    document.write(3, 4, QStringLiteral("电话"), headerFormat());
    document.write(3, 5, quote.customerPhone);
    writeHeaders(document, 5, {
        QStringLiteral("行号"), QStringLiteral("物料编码"), QStringLiteral("物料名称"),
        QStringLiteral("规格"), QStringLiteral("单位"), QStringLiteral("数量"),
        QStringLiteral("单价"), QStringLiteral("小计")
    });
    int row = 6;
    for (const auto& item : quote.items) {
        document.write(row, 1, item.lineNo);
        document.write(row, 2, item.materialCode);
        document.write(row, 3, item.materialName);
        document.write(row, 4, item.specification);
        document.write(row, 5, item.unit);
        document.write(row, 6, static_cast<double>(item.quantityMicros) / 1'000'000.0);
        writeMoney(document, row, 7, item.unitPriceCents);
        writeMoney(document, row, 8, item.subtotalCents);
        ++row;
    }
    ++row;
    document.write(row, 7, QStringLiteral("材料成本"), headerFormat());
    writeMoney(document, row++, 8, quote.materialCostCents);
    document.write(row, 7, QStringLiteral("运费"), headerFormat());
    writeMoney(document, row++, 8, quote.freightCents);
    document.write(row, 7, QStringLiteral("其他费用"), headerFormat());
    writeMoney(document, row++, 8, quote.otherFeesCents);
    document.write(row, 7, QStringLiteral("含税报价"), headerFormat());
    writeMoney(document, row, 8, quote.priceWithTaxCents);
    document.setColumnWidth(1, 10);
    document.setColumnWidth(2, 18);
    document.setColumnWidth(3, 24);
    document.setColumnWidth(4, 24);
    document.setColumnWidth(5, 10);
    document.setColumnWidth(6, 12);
    document.setColumnWidth(7, 14);
    document.setColumnWidth(8, 16);
    return save(document, output, error);
}

bool WorkbookService::exportStatistics(
    QIODevice* output,
    const StatisticsWorkbook& statistics,
    QString* error
) {
    Document document;
    renameFirstSheet(document, QStringLiteral("汇总"));
    document.mergeCells(QStringLiteral("A1:B1"), titleFormat());
    document.write(1, 1, statistics.title.isEmpty() ? QStringLiteral("报价统计") : statistics.title, titleFormat());
    writeHeaders(document, 3, {QStringLiteral("指标"), QStringLiteral("值")});
    const std::vector<std::pair<QString, QVariant>> values{
        {QStringLiteral("开始日期"), statistics.fromDate},
        {QStringLiteral("结束日期"), statistics.toDate},
        {QStringLiteral("报价数量"), statistics.summary.quoteCount},
        {QStringLiteral("报价金额"), static_cast<double>(statistics.summary.totalCents) / 100.0},
        {QStringLiteral("平均报价"), static_cast<double>(statistics.summary.averageCents) / 100.0},
        {QStringLiteral("已报价数量"), statistics.summary.issuedCount},
        {QStringLiteral("已作废数量"), statistics.summary.voidCount},
        {QStringLiteral("已报价占比"), statistics.summary.issuedRate},
    };
    int row = 4;
    for (const auto& [label, value] : values) {
        document.write(row, 1, label);
        if (label == QStringLiteral("报价金额") || label == QStringLiteral("平均报价")) {
            document.write(row, 2, value, moneyFormat());
        } else if (label == QStringLiteral("已报价占比")) {
            document.write(row, 2, value, percentageFormat());
        } else {
            document.write(row, 2, value);
        }
        ++row;
    }
    document.setColumnWidth(1, 22);
    document.setColumnWidth(2, 22);
    writeStatisticsRows(document, QStringLiteral("按月"), statistics.monthly);
    writeStatisticsRows(document, QStringLiteral("按客户"), statistics.customers);
    writeStatisticsRows(document, QStringLiteral("按产品类别"), statistics.categories);
    document.selectSheet(QStringLiteral("汇总"));
    return save(document, output, error);
}

} // namespace manage::excel
