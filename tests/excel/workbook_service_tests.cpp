#include "manage/excel/workbook_service.h"

#include <xlsxdocument.h>

#include <QBuffer>
#include <QCoreApplication>
#include <QFile>

#include <iostream>
#include <cmath>
#include <stdexcept>
#include <string>

namespace {

class TestFailure final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw TestFailure(message);
    }
}

void openBuffer(QBuffer& buffer) {
    require(buffer.open(QIODevice::ReadWrite), "buffer must open");
}

void rewindForReading(QBuffer& buffer) {
    buffer.close();
    require(buffer.open(QIODevice::ReadOnly), "buffer must reopen for reading");
}

void materialRoundTripUsesRealXlsx() {
    QBuffer output;
    openBuffer(output);
    manage::data::Material first;
    first.code = QStringLiteral("MAT-001");
    first.name = QStringLiteral("不锈钢螺栓");
    first.specification = QStringLiteral("M8x30");
    first.unit = QStringLiteral("件");
    first.category = QStringLiteral("紧固件");
    first.currentUnitPriceCents = 1234;
    first.isEnabled = true;

    QString error;
    require(
        manage::excel::WorkbookService::exportMaterials(&output, {first}, &error),
        "material workbook export"
    );
    require(output.data().startsWith("PK"), "xlsx must be a ZIP/OOXML file");
    rewindForReading(output);
    const auto imported = manage::excel::WorkbookService::importMaterials(&output);
    for (const auto& item : imported.errors) {
        std::cerr << "import error row=" << item.row
                  << " field=" << item.field.toStdString()
                  << " message=" << item.message.toStdString() << '\n';
    }
    require(imported.ok(), "exported material workbook must import");
    require(imported.rows.size() == 1, "round-trip row count");
    const auto& row = imported.rows.front();
    require(row.sourceRow == 2, "source row retained");
    require(row.material.code == QStringLiteral("MAT-001"), "material code round-trip");
    require(row.material.name == QStringLiteral("不锈钢螺栓"), "Chinese text round-trip");
    require(row.material.currentUnitPriceCents == 1234, "money remains numeric cents");
    require(row.material.isEnabled, "enabled round-trip");
}

void templateContainsInstructionsAndNumericPrice() {
    QBuffer output;
    openBuffer(output);
    QString error;
    require(
        manage::excel::WorkbookService::exportMaterialTemplate(&output, &error),
        "template export"
    );
    rewindForReading(output);
    QXlsx::Document document(&output);
    require(document.load(), "template opens as xlsx");
    require(document.sheetNames().contains(QStringLiteral("物料")), "material sheet exists");
    require(document.sheetNames().contains(QStringLiteral("填写说明")), "instruction sheet exists");
    require(document.selectSheet(QStringLiteral("物料")), "select material sheet");
    bool ok = false;
    const auto price = document.read(2, 6).toDouble(&ok);
    require(ok && std::abs(price - 12.34) < 0.000001, "price cell is numeric with two decimals");
}

void importerReportsSpecificRows() {
    QBuffer output;
    openBuffer(output);
    QXlsx::Document document;
    const QStringList headers{
        QStringLiteral("物料编码"), QStringLiteral("物料名称"), QStringLiteral("规格"),
        QStringLiteral("单位"), QStringLiteral("类别"), QStringLiteral("当前单价"),
        QStringLiteral("启用"), QStringLiteral("供应商名称"), QStringLiteral("供货周期（天）")
    };
    for (int column = 1; column <= headers.size(); ++column) {
        document.write(1, column, headers.at(column - 1));
    }
    document.write(2, 1, QStringLiteral("DUP-1"));
    document.write(2, 2, QStringLiteral("第一行"));
    document.write(2, 4, QStringLiteral("件"));
    document.write(2, 6, 1.001);
    document.write(2, 7, QStringLiteral("是"));
    document.write(3, 1, QStringLiteral("DUP-1"));
    document.write(3, 2, QStringLiteral("第二行"));
    document.write(3, 4, QStringLiteral("件"));
    document.write(3, 6, 2.00);
    document.write(3, 7, QStringLiteral("也许"));
    require(document.saveAs(&output), "invalid sample workbook save");
    rewindForReading(output);
    const auto imported = manage::excel::WorkbookService::importMaterials(&output);
    require(!imported.ok(), "invalid workbook rejected");
    bool priceError = false;
    bool duplicateError = false;
    bool enabledError = false;
    for (const auto& item : imported.errors) {
        priceError = priceError || (item.row == 2 && item.field == QStringLiteral("currentUnitPrice"));
        duplicateError = duplicateError || (item.row == 3 && item.field == QStringLiteral("code"));
        enabledError = enabledError || (item.row == 3 && item.field == QStringLiteral("isEnabled"));
    }
    require(priceError && duplicateError && enabledError, "row-specific errors returned");
}

void businessExportsHaveExpectedSheets() {
    manage::data::BomTemplate bom;
    bom.summary.code = QStringLiteral("BOM-1");
    bom.summary.name = QStringLiteral("产品一");
    bom.items.push_back({1, 10, 1, QStringLiteral("MAT-1"), QStringLiteral("材料"),
                         QStringLiteral("S"), QStringLiteral("件"), 2'000'000, {}});
    QBuffer bomOutput;
    openBuffer(bomOutput);
    require(manage::excel::WorkbookService::exportBom(&bomOutput, bom), "BOM export");
    rewindForReading(bomOutput);
    QXlsx::Document bomDocument(&bomOutput);
    require(bomDocument.load() && bomDocument.sheetNames().contains(QStringLiteral("BOM")),
            "BOM workbook opens");

    manage::data::QuoteDocument quote;
    quote.summary.quoteNumber = QStringLiteral("Q-001");
    quote.summary.customerName = QStringLiteral("客户甲");
    quote.priceWithTaxCents = 1200;
    quote.items.push_back({1, 10, 1, QStringLiteral("MAT-1"), QStringLiteral("材料"),
                           QStringLiteral("S"), QStringLiteral("件"), 1'000'000,
                           1000, 1000, {}});
    QBuffer quoteOutput;
    openBuffer(quoteOutput);
    require(manage::excel::WorkbookService::exportQuote(&quoteOutput, quote), "quote export");
    rewindForReading(quoteOutput);
    QXlsx::Document quoteDocument(&quoteOutput);
    require(quoteDocument.load() && quoteDocument.sheetNames().contains(QStringLiteral("报价单")),
            "quote workbook opens");

    manage::excel::StatisticsWorkbook statistics;
    statistics.title = QStringLiteral("报价统计");
    statistics.summary = {2, 3000, 1500, 1, 1, 0.5};
    statistics.monthly.push_back({QStringLiteral("2026-08"), 2, 3000});
    statistics.customers.push_back({QStringLiteral("客户甲"), 2, 3000});
    statistics.categories.push_back({QStringLiteral("紧固件"), 2, 3000});
    QBuffer statisticsOutput;
    openBuffer(statisticsOutput);
    require(manage::excel::WorkbookService::exportStatistics(&statisticsOutput, statistics),
            "statistics export");
    rewindForReading(statisticsOutput);
    QXlsx::Document statisticsDocument(&statisticsOutput);
    require(statisticsDocument.load(), "statistics workbook opens");
    require(statisticsDocument.sheetNames().size() == 4, "statistics has four sheets");
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    try {
        std::cerr << "[0] start\n";
        materialRoundTripUsesRealXlsx();
        std::cerr << "[1] roundtrip\n";
        templateContainsInstructionsAndNumericPrice();
        std::cerr << "[2] template\n";
        importerReportsSpecificRows();
        std::cerr << "[3] importer\n";
        businessExportsHaveExpectedSheets();
        std::cerr << "[4] business\n";
        if (argc > 1) {
            QFile sample(QString::fromLocal8Bit(argv[1]));
            require(sample.open(QIODevice::WriteOnly), "sample output file must open");
            manage::data::Material material;
            material.code = QStringLiteral("SAMPLE-001");
            material.name = QStringLiteral("示例物料");
            material.specification = QStringLiteral("M8x30");
            material.unit = QStringLiteral("件");
            material.category = QStringLiteral("紧固件");
            material.currentUnitPriceCents = 1234;
            require(manage::excel::WorkbookService::exportMaterials(&sample, {material}),
                    "sample workbook export");
        }
        std::cout << "Excel workbook tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Excel workbook tests failed: " << error.what() << '\n';
        return 1;
    }
}
