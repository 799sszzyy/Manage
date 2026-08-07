#include "manage/data/price_resolution.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <algorithm>
#include <utility>

namespace manage::data {

std::optional<int> matchCopperTierIndex(
    const std::vector<qint64>& thresholds,
    qint64 inputCopperCents
) {
    if (thresholds.empty()) {
        return std::nullopt;
    }
    // 1) 精确命中某档。
    for (std::size_t index = 0; index < thresholds.size(); ++index) {
        if (thresholds.at(index) == inputCopperCents) {
            return static_cast<int>(index);
        }
    }
    // 2) 取"档位 <= 当前铜价"中的最大档（向下取档）。
    for (std::size_t index = 0; index < thresholds.size(); ++index) {
        if (thresholds.at(index) > inputCopperCents) {
            return index == 0 ? 0 : static_cast<int>(index - 1);
        }
    }
    // 3) 当前铜价高于所有档位：取最高档。
    return static_cast<int>(thresholds.size() - 1);
}

namespace {

bool readMaterial(
    QSqlDatabase database,
    qint64 materialId,
    QString& code,
    QString& name,
    bool& isCopperBased,
    qint64& defaultUnitPriceCents,
    QString& errorMessage
) {
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "SELECT code, name, is_copper_based, current_unit_price_cents "
        "FROM materials WHERE id = :id"
    ));
    query.bindValue(QStringLiteral(":id"), materialId);
    if (!query.exec()) {
        errorMessage = query.lastError().text();
        return false;
    }
    if (!query.next()) {
        errorMessage = QStringLiteral("物料 %1 不存在").arg(materialId);
        return false;
    }
    code = query.value(0).toString();
    name = query.value(1).toString();
    isCopperBased = query.value(2).toBool();
    defaultUnitPriceCents = query.value(3).toLongLong();
    return true;
}

bool countEnabledSuppliers(
    QSqlDatabase database,
    qint64 materialId,
    qint64& count,
    QString& errorMessage
) {
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM material_suppliers "
        "WHERE material_id = :id AND is_enabled = TRUE"
    ));
    query.bindValue(QStringLiteral(":id"), materialId);
    if (!query.exec() || !query.next()) {
        errorMessage = query.lastError().text();
        return false;
    }
    count = query.value(0).toLongLong();
    return true;
}

bool readSupplier(
    QSqlDatabase database,
    qint64 supplierId,
    qint64& materialId,
    QString& supplierName,
    bool& enabled,
    QString& errorMessage
) {
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "SELECT material_id, supplier_name, is_enabled "
        "FROM material_suppliers WHERE id = :id"
    ));
    query.bindValue(QStringLiteral(":id"), supplierId);
    if (!query.exec()) {
        errorMessage = query.lastError().text();
        return false;
    }
    if (!query.next()) {
        errorMessage = QStringLiteral("供应商 %1 不存在").arg(supplierId);
        return false;
    }
    materialId = query.value(0).toLongLong();
    supplierName = query.value(1).toString();
    enabled = query.value(2).toBool();
    return true;
}

bool readPriceBranches(
    QSqlDatabase database,
    qint64 supplierId,
    std::vector<SupplierPriceBranch>& branches,
    QString& errorMessage
) {
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "SELECT id, copper_price_cents, unit_price_cents "
        "FROM material_supplier_prices "
        "WHERE material_supplier_id = :supplierId AND is_enabled = TRUE "
        "ORDER BY copper_price_cents"
    ));
    query.bindValue(QStringLiteral(":supplierId"), supplierId);
    if (!query.exec()) {
        errorMessage = query.lastError().text();
        return false;
    }
    while (query.next()) {
        SupplierPriceBranch branch;
        branch.priceId = query.value(0).toLongLong();
        if (!query.value(1).isNull()) {
            branch.copperPriceCents = query.value(1).toLongLong();
        }
        branch.unitPriceCents = query.value(2).toLongLong();
        branches.push_back(std::move(branch));
    }
    return true;
}

} // namespace

bool resolveMaterialPrice(
    QSqlDatabase database,
    qint64 materialId,
    qint64 materialSupplierId,
    std::optional<qint64> copperPriceCents,
    ResolvedMaterialPrice& result,
    QString& errorMessage
) {
    result = {};
    result.materialId = materialId;

    QString code;
    QString name;
    bool isCopperBased = false;
    qint64 defaultUnitPriceCents = 0;
    if (!readMaterial(
            database, materialId, code, name, isCopperBased, defaultUnitPriceCents, errorMessage
        )) {
        return false;
    }
    result.materialCode = code;
    result.materialName = name;
    result.isCopperBased = isCopperBased;

    qint64 supplierCount = 0;
    if (!countEnabledSuppliers(database, materialId, supplierCount, errorMessage)) {
        return false;
    }
    result.hasSuppliers = supplierCount > 0;

    // 未指定供应商：回退物料默认单价（兼容尚未建立供应商的物料）。
    if (materialSupplierId <= 0) {
        result.materialSupplierId = 0;
        result.supplierName.clear();
        result.copperPriceCents.reset();
        result.unitPriceCents = defaultUnitPriceCents;
        return true;
    }

    qint64 supplierMaterialId = 0;
    QString supplierName;
    bool supplierEnabled = false;
    if (!readSupplier(
            database,
            materialSupplierId,
            supplierMaterialId,
            supplierName,
            supplierEnabled,
            errorMessage
        )) {
        return false;
    }
    if (supplierMaterialId != materialId) {
        errorMessage = QStringLiteral(
            "供应商 %1 不属于物料 %2，请重新选择供应商"
        ).arg(materialSupplierId).arg(materialId);
        return false;
    }
    if (!supplierEnabled) {
        errorMessage = QStringLiteral(
            "供应商 %1 已停用，不能用于报价，请选择其他供应商"
        ).arg(supplierName);
        return false;
    }
    result.materialSupplierId = materialSupplierId;
    result.supplierName = supplierName;

    std::vector<SupplierPriceBranch> branches;
    if (!readPriceBranches(database, materialSupplierId, branches, errorMessage)) {
        return false;
    }

    if (isCopperBased) {
        // 电线类：必须输入当前铜价，按档匹配真实单价。
        if (!copperPriceCents.has_value() || *copperPriceCents <= 0) {
            errorMessage = QStringLiteral(
                "物料 %1 是电线类物料，必须输入当前铜价才能解析价格"
            ).arg(materialId);
            return false;
        }
        std::vector<qint64> thresholds;
        for (const auto& branch : branches) {
            if (branch.copperPriceCents.has_value()) {
                thresholds.push_back(*branch.copperPriceCents);
            }
        }
        const auto matched = matchCopperTierIndex(thresholds, *copperPriceCents);
        if (!matched.has_value()) {
            errorMessage = QStringLiteral(
                "供应商 %1 对物料 %2 没有可用的铜价档，请先在物料库维护价格"
            ).arg(supplierName).arg(materialId);
            return false;
        }
        // branches 与 thresholds 同序（SQL 已按铜价升序）。
        const auto& branch = branches.at(static_cast<std::size_t>(*matched));
        result.copperPriceCents = branch.copperPriceCents;
        result.unitPriceCents = branch.unitPriceCents;
        return true;
    }

    // 普通物料：取该供应商的普通价格档（铜价列为 NULL）。
    for (const auto& branch : branches) {
        if (!branch.copperPriceCents.has_value()) {
            result.copperPriceCents.reset();
            result.unitPriceCents = branch.unitPriceCents;
            return true;
        }
    }
    errorMessage = QStringLiteral(
        "供应商 %1 对物料 %2 没有可用的普通价格，请先在物料库维护价格"
    ).arg(supplierName).arg(materialId);
    return false;
}

} // namespace manage::data
