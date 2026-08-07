#pragma once

#include <QSqlDatabase>
#include <QString>
#include <QtGlobal>

#include <optional>
#include <vector>

namespace manage::data {

// 供应商价格档位：material_supplier_prices 的启用分支。
// 电线类物料按铜价区分（copperPriceCents 有值），普通物料为普通价（空）。
struct SupplierPriceBranch final {
    qint64 priceId{};
    std::optional<qint64> copperPriceCents;
    qint64 unitPriceCents{};
};

// 按（物料, 供应商, 当前铜价）解析出的真实价格结果。
struct ResolvedMaterialPrice final {
    qint64 materialId{};
    QString materialCode;
    QString materialName;
    qint64 materialSupplierId{};
    QString supplierName;
    bool isCopperBased{false};
    // 命中的铜价档（分）；普通物料为空。
    std::optional<qint64> copperPriceCents;
    qint64 unitPriceCents{};
    // 物料是否存在启用供应商（供调用方决定是否强制选择供应商）。
    bool hasSuppliers{false};
};

// 纯函数：铜价取档匹配（分）。
// 业务规则：优先取等于 input 的档；否则取"档位 <= input"中的最大档；
//          若 input 低于所有档位，取最小档（最低价兜底）。
// thresholds 须升序；成功返回命中的下标，无档位时返回 std::nullopt。
std::optional<int> matchCopperTierIndex(
    const std::vector<qint64>& thresholds,
    qint64 inputCopperCents
);

// 数据库解析：material + supplier + 当前铜价 -> 真实单价（分）。
//  - materialSupplierId <= 0 表示未指定供应商，回退物料默认单价
//    current_unit_price_cents（兼容尚无供应商的既有物料）。
//  - 指定供应商时校验其归属与启用状态，并按铜价档解析单价。
// 失败返回 false，errorMessage 为面向用户的中文业务提示。
bool resolveMaterialPrice(
    QSqlDatabase database,
    qint64 materialId,
    qint64 materialSupplierId,
    std::optional<qint64> copperPriceCents,
    ResolvedMaterialPrice& result,
    QString& errorMessage
);

} // namespace manage::data
