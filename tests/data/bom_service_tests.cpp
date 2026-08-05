#include "manage/data/bom_service.h"

#include <QHash>

#include <cstdlib>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using manage::data::BomItemInput;
using manage::data::BomRepositoryStatus;

class TestFailure final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw TestFailure(message);
    }
}

manage::data::BomTemplate makeTemplate(
    qint64 id,
    const QString& code,
    const QString& name,
    bool enabled,
    int revision,
    const std::vector<BomItemInput>& inputs = {}
) {
    manage::data::BomTemplate result;
    result.summary = {id, code, name, QStringLiteral("description"), enabled, revision};
    for (const auto& input : inputs) {
        result.items.push_back({
            static_cast<qint64>(result.items.size() + 1),
            input.lineNo,
            input.materialId,
            QStringLiteral("MAT-%1").arg(input.materialId),
            QStringLiteral("material"),
            QString(),
            QStringLiteral("piece"),
            input.quantityMicros,
            input.notes,
        });
    }
    return result;
}

class FakeBomRepository final : public manage::data::BomRepository {
public:
    QHash<qint64, bool> materials;
    int createCalls{};
    manage::data::NewBomTemplate lastCreate;
    BomRepositoryStatus mutationStatus{BomRepositoryStatus::Success};

    BomRepositoryStatus list(
        const manage::data::BomSearchQuery& query,
        manage::data::BomPage& result,
        QString&
    ) override {
        result.page = query.page;
        result.pageSize = query.pageSize;
        result.total = 1;
        result.items.push_back({1, QStringLiteral("BOM-1"),
                                QStringLiteral("Assembly"), QString(), true, 1});
        return BomRepositoryStatus::Success;
    }

    BomRepositoryStatus getById(
        qint64 id,
        std::optional<manage::data::BomTemplate>& result,
        QString&
    ) override {
        if (id != 1) {
            result.reset();
            return BomRepositoryStatus::NotFound;
        }
        result = makeTemplate(1, QStringLiteral("BOM-1"),
                              QStringLiteral("Assembly"), true, 1);
        return BomRepositoryStatus::Success;
    }

    BomRepositoryStatus lookupMaterials(
        const std::vector<qint64>& ids,
        std::vector<manage::data::MaterialReference>& result,
        QString&
    ) override {
        result.clear();
        for (const auto id : ids) {
            if (materials.contains(id)) {
                result.push_back({id, materials.value(id)});
            }
        }
        return BomRepositoryStatus::Success;
    }

    BomRepositoryStatus create(
        const manage::data::NewBomTemplate& command,
        manage::data::BomTemplate& result,
        QString& errorMessage
    ) override {
        ++createCalls;
        lastCreate = command;
        if (mutationStatus != BomRepositoryStatus::Success) {
            if (mutationStatus == BomRepositoryStatus::InvalidMaterial) {
                errorMessage = QStringLiteral("material was disabled during save");
            }
            return mutationStatus;
        }
        result = makeTemplate(10, command.code, command.name,
                              command.isEnabled, 1, command.items);
        return BomRepositoryStatus::Success;
    }

    BomRepositoryStatus update(
        const manage::data::UpdateBomTemplate& command,
        manage::data::BomTemplate& result,
        QString&
    ) override {
        if (mutationStatus != BomRepositoryStatus::Success) {
            return mutationStatus;
        }
        result = makeTemplate(command.id, command.code, command.name,
                              true, command.expectedRevision + 1);
        return BomRepositoryStatus::Success;
    }

    BomRepositoryStatus setEnabled(
        const manage::data::SetBomEnabled& command,
        manage::data::BomTemplate& result,
        QString&
    ) override {
        if (mutationStatus != BomRepositoryStatus::Success) {
            return mutationStatus;
        }
        result = makeTemplate(command.id, QStringLiteral("BOM-1"),
                              QStringLiteral("Assembly"), command.isEnabled,
                              command.expectedRevision + 1);
        return BomRepositoryStatus::Success;
    }

    BomRepositoryStatus replaceItems(
        const manage::data::ReplaceBomItems& command,
        manage::data::BomTemplate& result,
        QString&
    ) override {
        if (mutationStatus != BomRepositoryStatus::Success) {
            return mutationStatus;
        }
        result = makeTemplate(command.id, QStringLiteral("BOM-1"),
                              QStringLiteral("Assembly"), true,
                              command.expectedRevision + 1, command.items);
        return BomRepositoryStatus::Success;
    }
};

void createNormalizesAndValidatesMaterials() {
    FakeBomRepository repository;
    repository.materials.insert(11, true);
    repository.materials.insert(12, true);
    manage::data::BomService service(repository);

    manage::data::NewBomTemplate command;
    command.code = QStringLiteral("  BOM-A  ");
    command.name = QStringLiteral("  Pump assembly  ");
    command.items = {
        {10, 11, 1'500'000, QStringLiteral("body")},
        {20, 12, 2'000'000, QStringLiteral("bolt")},
    };
    const auto result = service.create(command);

    require(result.ok(), "valid BOM must be created");
    require(repository.createCalls == 1, "repository create called once");
    require(repository.lastCreate.code == QStringLiteral("BOM-A"),
            "BOM code trimmed");
    require(repository.lastCreate.name == QStringLiteral("Pump assembly"),
            "BOM name trimmed");
    require(result.value->items.size() == 2, "created BOM returns items");
}

void duplicateLinesAndMaterialsAreRejected() {
    FakeBomRepository repository;
    repository.materials.insert(11, true);
    repository.materials.insert(12, true);
    manage::data::BomService service(repository);

    manage::data::NewBomTemplate duplicateLine;
    duplicateLine.code = QStringLiteral("BOM-A");
    duplicateLine.name = QStringLiteral("Assembly");
    duplicateLine.items = {{10, 11, 1, {}}, {10, 12, 1, {}}};
    const auto lineResult = service.create(duplicateLine);
    require(!lineResult.ok(), "duplicate line numbers rejected");
    require(lineResult.error == manage::data::BomErrorCode::Validation,
            "duplicate line is validation error");

    auto duplicateMaterial = duplicateLine;
    duplicateMaterial.items = {{10, 11, 1, {}}, {20, 11, 1, {}}};
    const auto materialResult = service.create(duplicateMaterial);
    require(!materialResult.ok(), "duplicate materials rejected");
    require(repository.createCalls == 0,
            "invalid BOM must not reach repository create");
}

void invalidMaterialReferencesAreRejected() {
    FakeBomRepository repository;
    repository.materials.insert(11, false);
    manage::data::BomService service(repository);

    manage::data::NewBomTemplate disabled;
    disabled.code = QStringLiteral("BOM-A");
    disabled.name = QStringLiteral("Assembly");
    disabled.items = {{10, 11, 1'000'000, {}}};
    const auto disabledResult = service.create(disabled);
    require(!disabledResult.ok(), "disabled material rejected");
    require(disabledResult.message.contains(QStringLiteral("disabled")),
            "disabled material message");

    disabled.items = {{10, 99, 1'000'000, {}}};
    const auto missingResult = service.create(disabled);
    require(!missingResult.ok(), "missing material rejected");
    require(missingResult.message.contains(QStringLiteral("does not exist")),
            "missing material message");

    disabled.items = {{10, 11, 0, {}}};
    const auto quantityResult = service.create(disabled);
    require(!quantityResult.ok(), "zero quantity rejected");
}

void optimisticConflictIsExposed() {
    FakeBomRepository repository;
    repository.mutationStatus = BomRepositoryStatus::Conflict;
    manage::data::BomService service(repository);

    const manage::data::UpdateBomTemplate command{
        1,
        QStringLiteral("BOM-A"),
        QStringLiteral("Assembly"),
        QString(),
        2,
    };
    const auto result = service.update(command);
    require(!result.ok(), "conflicting revision must fail");
    require(result.error == manage::data::BomErrorCode::Conflict,
            "repository conflict mapped to service conflict");
}

void transactionMaterialFailureIsExposedAsValidation() {
    FakeBomRepository repository;
    repository.materials.insert(11, true);
    repository.mutationStatus = BomRepositoryStatus::InvalidMaterial;
    manage::data::BomService service(repository);

    manage::data::NewBomTemplate command;
    command.code = QStringLiteral("BOM-RACE");
    command.name = QStringLiteral("Concurrent material change");
    command.items = {{10, 11, 1'000'000, {}}};
    const auto result = service.create(command);
    require(!result.ok(), "transaction-time disabled material must fail");
    require(result.error == manage::data::BomErrorCode::Validation,
            "transaction material failure maps to validation error");
    require(result.message.contains(QStringLiteral("disabled")),
            "transaction material failure keeps repository message");
}

void listAndReplaceItemsUseValidatedInputs() {
    FakeBomRepository repository;
    repository.materials.insert(11, true);
    manage::data::BomService service(repository);

    manage::data::BomSearchQuery invalidSearch;
    invalidSearch.pageSize = 101;
    require(!service.list(invalidSearch).ok(), "oversized page rejected");

    manage::data::BomSearchQuery validSearch;
    validSearch.page = 2;
    validSearch.pageSize = 10;
    const auto page = service.list(validSearch);
    require(page.ok() && page.value->page == 2 && page.value->pageSize == 10,
            "validated paging reaches repository");

    const manage::data::ReplaceBomItems replacement{
        1,
        1,
        {{10, 11, 3'000'000, QStringLiteral("three pieces")}},
    };
    const auto replaced = service.replaceItems(replacement);
    require(replaced.ok(), "valid item replacement succeeds");
    require(replaced.value->summary.revision == 2,
            "item replacement returns new revision");
    require(replaced.value->items.size() == 1,
            "item replacement returns current items");
}

} // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"create validates materials", createNormalizesAndValidatesMaterials},
        {"duplicate items", duplicateLinesAndMaterialsAreRejected},
        {"material references", invalidMaterialReferencesAreRejected},
        {"optimistic conflict", optimisticConflictIsExposed},
        {"transaction material validation", transactionMaterialFailureIsExposedAsValidation},
        {"list and replace", listAndReplaceItemsUseValidatedInputs},
    };

    std::size_t passed = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            ++passed;
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
        }
    }
    std::cout << passed << "/" << tests.size() << " tests passed\n";
    return passed == tests.size() ? EXIT_SUCCESS : EXIT_FAILURE;
}
