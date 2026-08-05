#pragma once

#include "manage/data/catalog_models.h"

#include <QSqlDatabase>
#include <QString>

#include <vector>

namespace manage::data {

struct MaterialBatchRow final {
    int sourceRow{};
    MaterialDraft material;
};

struct MaterialBatchError final {
    int sourceRow{};
    QString field;
    QString message;
};

struct MaterialBatchResult final {
    int totalRows{};
    int createCount{};
    int updateCount{};
    bool committed{};
    std::vector<MaterialBatchError> errors;

    [[nodiscard]] bool ok() const noexcept { return errors.empty(); }
};

class MaterialBatchService final {
public:
    static constexpr int maximumRows = 5'000;

    explicit MaterialBatchService(QSqlDatabase database);

    [[nodiscard]] MaterialBatchResult importMaterials(
        std::vector<MaterialBatchRow> rows,
        bool validateOnly
    );

private:
    QSqlDatabase database_;
};

} // namespace manage::data
