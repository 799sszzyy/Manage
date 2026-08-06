#pragma once

#include "manage/data/catalog_models.h"

#include <QString>

#include <vector>

namespace manage::excel {

struct WorkbookError final {
    int row{};
    QString field;
    QString message;
};

struct ImportedMaterialRow final {
    int sourceRow{};
    manage::data::MaterialDraft material;
    // 可选的供应商分支：供应商名称非空时导入会同步创建/更新供应商，
    // 并写入交货周期（天）。
    QString supplierName;
    int leadDays{0};
};

struct MaterialImportResult final {
    std::vector<ImportedMaterialRow> rows;
    std::vector<WorkbookError> errors;

    [[nodiscard]] bool ok() const noexcept { return errors.empty(); }
};

struct StatisticsSummary final {
    qint64 quoteCount{};
    qint64 totalCents{};
    qint64 averageCents{};
    qint64 issuedCount{};
    qint64 voidCount{};
    double publishedRate{};
};

struct StatisticsRow final {
    QString dimension;
    qint64 quoteCount{};
    qint64 totalCents{};
};

struct StatisticsWorkbook final {
    QString title;
    QString fromDate;
    QString toDate;
    StatisticsSummary summary;
    std::vector<StatisticsRow> monthly;
    std::vector<StatisticsRow> customers;
    std::vector<StatisticsRow> categories;
};

} // namespace manage::excel
