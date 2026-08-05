#include "manage/data/catalog_service.h"

#include "support/in_memory_catalog_repository.h"

#include <QCoreApplication>

#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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

manage::data::MaterialDraft validMaterial() {
    return {
        QStringLiteral(" MAT-001 "),
        QStringLiteral(" Steel plate "),
        QStringLiteral(" 2 mm "),
        QStringLiteral(" sheet "),
        QStringLiteral(" metal "),
        12'345,
        true,
    };
}

void materialValidationAndNormalization() {
    auto repository = std::make_shared<InMemoryCatalogRepository>();
    manage::data::CatalogService service(repository);

    auto draft = validMaterial();
    const auto created = service.createMaterial(draft);
    require(created.ok(), "valid material must be created");
    require(created.value->code == QStringLiteral("MAT-001"), "code must be trimmed");
    require(created.value->name == QStringLiteral("Steel plate"), "name must be trimmed");
    require(created.value->currentUnitPriceCents == 12'345, "price uses cents");

    draft.currentUnitPriceCents = -1;
    const auto invalid = service.createMaterial(draft);
    require(!invalid.ok(), "negative material price must fail");
    require(
        invalid.error->code == manage::data::CatalogErrorCode::InvalidRequest,
        "negative price error code"
    );
    require(
        invalid.error->field == QStringLiteral("currentUnitPriceCents"),
        "negative price error field"
    );
}

void materialPagingSearchAndEnabledFilter() {
    auto repository = std::make_shared<InMemoryCatalogRepository>();
    manage::data::CatalogService service(repository);
    require(service.createMaterial(validMaterial()).ok(), "first material");

    auto second = validMaterial();
    second.code = QStringLiteral("WOOD-001");
    second.name = QStringLiteral("Wood board");
    second.category = QStringLiteral("wood");
    second.isEnabled = false;
    require(service.createMaterial(second).ok(), "second material");

    manage::data::PageQuery query;
    query.search = QStringLiteral("wood");
    query.enabled = false;
    const auto page = service.listMaterials(query);
    require(page.ok(), "filtered material list");
    require(page.value->total == 1, "search and enabled filter total");
    require(page.value->items.front().code == QStringLiteral("WOOD-001"), "filter result");

    query.pageSize = 101;
    const auto invalid = service.listMaterials(query);
    require(!invalid.ok(), "page size above 100 must fail");
    require(invalid.error->field == QStringLiteral("pageSize"), "page size error field");

    query.pageSize = 20;
    query.page = std::numeric_limits<int>::max();
    const auto hugePage = service.listMaterials(query);
    require(!hugePage.ok(), "extremely large page must fail before offset calculation");
    require(hugePage.error->field == QStringLiteral("page"), "large page error field");
}

void optimisticRevisionPreventsLostUpdates() {
    auto repository = std::make_shared<InMemoryCatalogRepository>();
    manage::data::CatalogService service(repository);
    const auto created = service.createMaterial(validMaterial());
    require(created.ok(), "material create before update");

    auto changed = validMaterial();
    changed.name = QStringLiteral("Updated plate");
    const auto updated = service.updateMaterial(created.value->id, 1, changed);
    require(updated.ok(), "first update must pass");
    require(updated.value->revision == 2, "revision must increment");

    changed.name = QStringLiteral("Stale overwrite");
    const auto stale = service.updateMaterial(created.value->id, 1, changed);
    require(!stale.ok(), "stale revision must fail");
    require(
        stale.error->code == manage::data::CatalogErrorCode::RevisionConflict,
        "stale update structured code"
    );
    require(stale.error->field == QStringLiteral("revision"), "conflict field");
}

void customerLifecycleUsesRevision() {
    auto repository = std::make_shared<InMemoryCatalogRepository>();
    manage::data::CatalogService service(repository);
    manage::data::CustomerDraft draft{
        QStringLiteral(" Example Ltd. "),
        QStringLiteral(" Alice "),
        QStringLiteral(" 123456 "),
        QStringLiteral(" Main Street "),
        QStringLiteral(" Priority customer "),
    };
    const auto created = service.createCustomer(draft);
    require(created.ok(), "valid customer must be created");
    require(created.value->name == QStringLiteral("Example Ltd."), "customer name trimmed");

    draft.phone = QStringLiteral("654321");
    const auto updated = service.updateCustomer(created.value->id, 1, draft);
    require(updated.ok(), "customer update must pass");
    require(updated.value->phone == QStringLiteral("654321"), "customer phone updated");
    require(updated.value->revision == 2, "customer revision increments");

    const auto stale = service.updateCustomer(created.value->id, 1, draft);
    require(!stale.ok(), "stale customer update must fail");
    require(
        stale.error->code == manage::data::CatalogErrorCode::RevisionConflict,
        "customer conflict code"
    );
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"material validation", materialValidationAndNormalization},
        {"material paging", materialPagingSearchAndEnabledFilter},
        {"material optimistic revision", optimisticRevisionPreventsLostUpdates},
        {"customer lifecycle", customerLifecycleUsesRevision},
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
