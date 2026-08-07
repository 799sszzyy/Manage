#pragma once

#include <QtGlobal>
#include <QString>

#include <optional>
#include <vector>

namespace manage::data {

struct BomItemInput final {
    int lineNo{};
    qint64 materialId{};
    qint64 quantityMicros{};
    QString notes;
    // 批次9：构建 BOM 时同一物料选择供应商 + 输入当前铜价解析真实单价。
    // 0 表示未指定供应商（回退物料默认单价，兼容尚无供应商的物料）。
    qint64 materialSupplierId{};
    // 当前铜价（元/吨，精确到分）；电线类物料指定供应商后必填，普通物料为空。
    std::optional<qint64> copperPriceCents;
};

struct BomItem final {
    qint64 id{};
    int lineNo{};
    qint64 materialId{};
    QString materialCode;
    QString materialName;
    QString materialSpecification;
    QString materialUnit;
    qint64 quantityMicros{};
    QString notes;
    // 批次9：供应商引用与名称快照、解析单价快照（历史价格不受调价影响）。
    qint64 materialSupplierId{};
    QString supplierName;
    std::optional<qint64> copperPriceCents;
    qint64 unitPriceCents{};
};

// 服务端按（供应商, 铜价档）解析出的条目价格，随 BOM 条目一起落库。
struct BomItemPricing final {
    qint64 materialSupplierId{};
    QString supplierName;
    std::optional<qint64> copperPriceCents;
    qint64 unitPriceCents{};
};

struct BomTemplateSummary final {
    qint64 id{};
    QString code;
    QString name;
    QString description;
    bool isEnabled{true};
    int revision{1};
};

struct BomTemplate final {
    BomTemplateSummary summary;
    std::vector<BomItem> items;
};

struct BomPage final {
    std::vector<BomTemplateSummary> items;
    qint64 total{};
    int page{1};
    int pageSize{20};
};

struct BomSearchQuery final {
    int page{1};
    int pageSize{20};
    QString search;
    std::optional<bool> enabled;
};

struct NewBomTemplate final {
    QString code;
    QString name;
    QString description;
    bool isEnabled{true};
    std::vector<BomItemInput> items;
};

struct UpdateBomTemplate final {
    qint64 id{};
    QString code;
    QString name;
    QString description;
    int expectedRevision{};
};

struct SetBomEnabled final {
    qint64 id{};
    bool isEnabled{};
    int expectedRevision{};
};

struct ReplaceBomItems final {
    qint64 id{};
    int expectedRevision{};
    std::vector<BomItemInput> items;
};

struct MaterialReference final {
    qint64 id{};
    bool isEnabled{};
};

} // namespace manage::data
