#pragma once

#include "manage/excel/workbook_models.h"

#include "manage/data/bom_models.h"
#include "manage/data/quote_models.h"

#include <QIODevice>
#include <QString>

#include <vector>

namespace manage::excel {

class WorkbookService final {
public:
    static constexpr int maximumMaterialRows = 5'000;

    [[nodiscard]] static bool exportMaterialTemplate(
        QIODevice* output,
        QString* error = nullptr
    );
    [[nodiscard]] static bool exportMaterials(
        QIODevice* output,
        const std::vector<manage::data::Material>& materials,
        QString* error = nullptr
    );
    [[nodiscard]] static MaterialImportResult importMaterials(QIODevice* input);

    [[nodiscard]] static bool exportBom(
        QIODevice* output,
        const manage::data::BomTemplate& bom,
        QString* error = nullptr
    );
    [[nodiscard]] static bool exportQuote(
        QIODevice* output,
        const manage::data::QuoteDocument& quote,
        QString* error = nullptr
    );
    [[nodiscard]] static bool exportStatistics(
        QIODevice* output,
        const StatisticsWorkbook& statistics,
        QString* error = nullptr
    );
};

} // namespace manage::excel
