#include "manage/data/material_batch_service.h"

#include <QCoreApplication>
#include <QSqlDatabase>

#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void invalidRowsFailBeforeDatabaseAccess() {
    manage::data::MaterialBatchService service(QSqlDatabase{});
    manage::data::MaterialDraft invalid;
    invalid.code = QStringLiteral("bad code with spaces");
    invalid.name = QStringLiteral(" ");
    invalid.unit = QStringLiteral("件");
    const auto result = service.importMaterials({{8, invalid}}, true);
    require(!result.ok(), "invalid batch rejected");
    require(result.errors.size() >= 2, "all validation errors returned");
    require(result.errors.front().sourceRow == 8, "source row retained");
    require(result.errors.front().field != QStringLiteral("database"),
            "validation happens before database access");
}

void duplicateCodesFailAsOneBatch() {
    manage::data::MaterialBatchService service(QSqlDatabase{});
    manage::data::MaterialDraft material;
    material.code = QStringLiteral("MAT-1");
    material.name = QStringLiteral("材料");
    material.unit = QStringLiteral("件");
    const auto result = service.importMaterials({{2, material}, {5, material}}, false);
    require(!result.ok() && !result.committed, "duplicate batch cannot commit");
    bool found = false;
    for (const auto& error : result.errors) {
        found = found || (error.sourceRow == 5 && error.field == QStringLiteral("code"));
    }
    require(found, "duplicate reports exact source row");
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    try {
        invalidRowsFailBeforeDatabaseAccess();
        duplicateCodesFailAsOneBatch();
        std::cout << "Material batch tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Material batch tests failed: " << error.what() << '\n';
        return 1;
    }
}
