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

void supplierBranchSupportsMultipleSuppliers() {
    auto repository = std::make_shared<InMemoryCatalogRepository>();
    manage::data::CatalogService service(repository);
    const auto material = service.createMaterial(validMaterial());
    require(material.ok(), "material must be created for supplier branch");

    manage::data::MaterialSupplierDraft first{
        QStringLiteral(" 供应商甲 "),
        QStringLiteral(" 张三 "),
        QStringLiteral(" 13800000001 "),
        false,
        true,
    };
    const auto created = service.createMaterialSupplier(material.value->id, first);
    require(created.ok(), "first supplier must be created");
    require(
        created.value->supplierName == QStringLiteral("供应商甲"),
        "supplier name must be trimmed"
    );
    require(created.value->revision == 1, "first supplier revision");

    // 第二个供应商：同一物料可维护多个供应商分支
    manage::data::MaterialSupplierDraft second{
        QStringLiteral("供应商乙"),
        QStringLiteral("李四"),
        QStringLiteral("13800000002"),
        false,
        true,
    };
    const auto created2 = service.createMaterialSupplier(material.value->id, second);
    require(created2.ok(), "second supplier must be created");
    require(created2.value->id != created.value->id, "supplier ids must differ");

    // 同一物料下供应商名称唯一
    const auto duplicate = service.createMaterialSupplier(material.value->id, first);
    require(!duplicate.ok(), "duplicate supplier name must fail");
    require(
        duplicate.error->code == manage::data::CatalogErrorCode::DuplicateCode,
        "duplicate supplier error code"
    );

    // 列表包含两个供应商
    manage::data::PageQuery query;
    const auto listed = service.listMaterialSuppliers(material.value->id, query);
    require(listed.ok(), "supplier list must succeed");
    require(listed.value->total == 2, "supplier list total");
    require(listed.value->items.size() == 2, "supplier list item count");

    // 更新与启停
    manage::data::MaterialSupplierDraft changed = second;
    changed.phone = QStringLiteral("13900000000");
    const auto updated = service.updateMaterialSupplier(
        created2.value->id,
        created2.value->revision,
        changed
    );
    require(updated.ok(), "supplier update must pass");
    require(updated.value->phone == QStringLiteral("13900000000"), "supplier phone updated");
    require(updated.value->revision == 2, "supplier revision increments");

    const auto disabled = service.setMaterialSupplierEnabled(
        created.value->id,
        created.value->revision,
        false
    );
    require(disabled.ok(), "supplier disable must pass");
    require(!disabled.value->isEnabled, "supplier must be disabled");

    // 校验：空供应商名称被拒绝
    manage::data::MaterialSupplierDraft emptyName{
        QStringLiteral("   "),
        QString(),
        QString(),
        false,
        true,
    };
    const auto invalid = service.createMaterialSupplier(material.value->id, emptyName);
    require(!invalid.ok(), "empty supplier name must fail");
    require(
        invalid.error->field == QStringLiteral("supplierName"),
        "supplier name validation field"
    );
}

void priceBranchSupportsCopperAndPlainPrices() {
    auto repository = std::make_shared<InMemoryCatalogRepository>();
    manage::data::CatalogService service(repository);
    const auto material = service.createMaterial(validMaterial());
    require(material.ok(), "material must be created for price branch");
    manage::data::MaterialSupplierDraft supplierDraft{
        QStringLiteral("供应商甲"),
        QString(),
        QString(),
        false,
        true,
    };
    const auto supplier = service.createMaterialSupplier(material.value->id, supplierDraft);
    require(supplier.ok(), "supplier must be created for price branch");

    // 普通物料价格分支：铜价为 null
    manage::data::MaterialPriceDraft plainPrice;
    plainPrice.unitPriceCents = 12'345;
    const auto plain = service.createMaterialPrice(supplier.value->id, plainPrice);
    require(plain.ok(), "plain price must be created");
    require(!plain.value->copperPriceCents.has_value(), "plain price has no copper price");

    // 电线类物料：同一供应商按不同铜价建立不同价格
    manage::data::MaterialPriceDraft copperLow;
    copperLow.copperPriceCents = 7'200;
    copperLow.unitPriceCents = 10'000;
    const auto low = service.createMaterialPrice(supplier.value->id, copperLow);
    require(low.ok(), "copper price branch must be created");
    require(
        low.value->copperPriceCents.has_value() && *low.value->copperPriceCents == 7'200,
        "copper price value"
    );

    manage::data::MaterialPriceDraft copperHigh;
    copperHigh.copperPriceCents = 7'800;
    copperHigh.unitPriceCents = 12'000;
    const auto high = service.createMaterialPrice(supplier.value->id, copperHigh);
    require(high.ok(), "second copper price branch must be created");

    // 同一供应商同一铜价唯一
    const auto duplicate = service.createMaterialPrice(supplier.value->id, copperLow);
    require(!duplicate.ok(), "duplicate copper price must fail");
    require(
        duplicate.error->code == manage::data::CatalogErrorCode::DuplicateCode,
        "duplicate copper price error code"
    );

    // 列表包含三条价格
    manage::data::PageQuery query;
    const auto listed = service.listMaterialPrices(supplier.value->id, query);
    require(listed.ok(), "price list must succeed");
    require(listed.value->total == 3, "price list total");

    // 校验：负铜价和负单价被拒绝
    manage::data::MaterialPriceDraft negativeCopper;
    negativeCopper.copperPriceCents = -1;
    const auto invalidCopper = service.createMaterialPrice(supplier.value->id, negativeCopper);
    require(!invalidCopper.ok(), "negative copper price must fail");
    require(
        invalidCopper.error->field == QStringLiteral("copperPriceCents"),
        "copper price validation field"
    );

    manage::data::MaterialPriceDraft negativeUnit;
    negativeUnit.unitPriceCents = -1;
    const auto invalidUnit = service.createMaterialPrice(supplier.value->id, negativeUnit);
    require(!invalidUnit.ok(), "negative unit price must fail");

    // 更新价格分支
    manage::data::MaterialPriceDraft updatedPrice = copperHigh;
    updatedPrice.unitPriceCents = 12'500;
    const auto updated = service.updateMaterialPrice(
        high.value->id,
        high.value->revision,
        updatedPrice
    );
    require(updated.ok(), "price update must pass");
    require(updated.value->unitPriceCents == 12'500, "price updated value");
    require(updated.value->revision == 2, "price revision increments");
}

void bundleCommitsMaterialSuppliersAndPricesAtomically() {
    auto repository = std::make_shared<InMemoryCatalogRepository>();
    manage::data::CatalogService service(repository);

    // 三级向导整包：一个物料 + 两个供应商（各自价格，同一供应商两条铜价价格）。
    manage::data::MaterialBundleDraft bundle;
    bundle.material = validMaterial();

    manage::data::MaterialBundleSupplierDraft first;
    first.supplier = {QStringLiteral("供应商甲"), QString(), QString(), false, true, 7};
    first.prices.push_back({std::nullopt, 10'000, false, true});
    first.prices.push_back({7'200, 11'000, false, true});
    first.prices.push_back({7'800, 12'000, false, true});

    manage::data::MaterialBundleSupplierDraft second;
    second.supplier = {QStringLiteral("供应商乙"), QString(), QString(), false, true, 10};
    second.prices.push_back({std::nullopt, 9'500, false, true});

    bundle.suppliers.push_back(first);
    bundle.suppliers.push_back(second);

    const auto result = service.createMaterialBundle(std::move(bundle));
    require(result.ok(), "bundle commit must succeed");
    require(result.value->material.id > 0, "bundle material id assigned");
    require(
        result.value->material.name == QStringLiteral("Steel plate"),
        "bundle material fields normalized"
    );
    require(result.value->suppliers.size() == 2, "bundle creates both suppliers");
    require(result.value->prices.size() == 4, "bundle creates all prices");

    // 供应商与价格的归属关系：每个价格挂在正确供应商下。
    const auto& supplierA = result.value->suppliers.at(0);
    const auto& supplierB = result.value->suppliers.at(1);
    require(supplierA.leadDays == 7, "first supplier lead days");
    require(supplierB.leadDays == 10, "second supplier lead days");
    std::size_t supplierAPrices = 0;
    std::size_t supplierBPrices = 0;
    for (const auto& price : result.value->prices) {
        if (price.supplierId == supplierA.id) {
            ++supplierAPrices;
        } else if (price.supplierId == supplierB.id) {
            ++supplierBPrices;
        }
    }
    require(supplierAPrices == 3, "supplier A keeps three copper-price rows");
    require(supplierBPrices == 1, "supplier B keeps one plain price row");

    // 提交后可通过现有接口读到同一物料。
    const auto listed = service.listMaterials({});
    require(listed.ok() && listed.value->total == 1, "bundle material listed");
}

void bundleRejectsInvalidSupplierOrPrice() {
    auto repository = std::make_shared<InMemoryCatalogRepository>();
    manage::data::CatalogService service(repository);

    manage::data::MaterialBundleDraft bundle;
    bundle.material = validMaterial();

    // 供应商名称为空 → 整包拒绝。
    manage::data::MaterialBundleSupplierDraft badSupplier;
    badSupplier.supplier = {QStringLiteral("   "), QString(), QString(), false, true, 0};
    bundle.suppliers.push_back(badSupplier);
    const auto invalid = service.createMaterialBundle(std::move(bundle));
    require(!invalid.ok(), "bundle with blank supplier name must fail");
    require(
        invalid.error->field == QStringLiteral("supplierName"),
        "bundle supplier validation field"
    );

    // 合法的物料 + 供应商，但价格铜价为负 → 整包拒绝。
    manage::data::MaterialBundleDraft bundle2;
    bundle2.material = validMaterial();
    manage::data::MaterialBundleSupplierDraft goodSupplier;
    goodSupplier.supplier = {QStringLiteral("供应商甲"), QString(), QString(), false, true, 5};
    manage::data::MaterialPriceDraft badPrice;
    badPrice.copperPriceCents = -1;
    goodSupplier.prices.push_back(badPrice);
    bundle2.suppliers.push_back(goodSupplier);
    const auto invalid2 = service.createMaterialBundle(std::move(bundle2));
    require(!invalid2.ok(), "bundle with negative copper price must fail");
    require(
        invalid2.error->field == QStringLiteral("copperPriceCents"),
        "bundle price validation field"
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
        {"supplier branch multi-supplier", supplierBranchSupportsMultipleSuppliers},
        {"price branch copper and plain", priceBranchSupportsCopperAndPlainPrices},
        {"bundle atomic commit", bundleCommitsMaterialSuppliersAndPricesAtomically},
        {"bundle rejects invalid data", bundleRejectsInvalidSupplierOrPrice},
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
