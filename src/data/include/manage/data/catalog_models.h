#pragma once

#include <QDateTime>
#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

namespace manage::data {

struct PageQuery final {
    int page{1};
    int pageSize{20};
    QString search;
    std::optional<bool> enabled;
};

template <typename T>
struct Page final {
    std::vector<T> items;
    std::int64_t total{};
    int page{1};
    int pageSize{20};
};

struct Material final {
    std::int64_t id{};
    QString code;
    QString name;
    QString specification;
    QString unit;
    QString category;
    std::int64_t currentUnitPriceCents{};
    bool isEnabled{true};
    // 电线类物料标志，按铜价定价；新增字段置于末尾以兼容聚合初始化。
    bool isCopperBased{false};
    std::uint32_t revision{1};
    QDateTime createdAt;
    QDateTime updatedAt;
};

struct MaterialDraft final {
    QString code;
    QString name;
    QString specification;
    QString unit;
    QString category;
    std::int64_t currentUnitPriceCents{};
    bool isEnabled{true};
    // 电线类物料标志，按铜价定价；新增字段置于末尾以兼容聚合初始化。
    bool isCopperBased{false};
};

// 物料下的供应商分支：同一编号物料可对应多个供应商。
struct MaterialSupplier final {
    std::int64_t id{};
    std::int64_t materialId{};
    QString supplierName;
    QString contactName;
    QString phone;
    bool isDefault{false};
    bool isEnabled{true};
    // 该供应商对当前物料的交货周期（天），用于计算 BOM 交期。
    // 新增字段置于末尾以兼容聚合初始化。
    int leadDays{0};
    std::uint32_t revision{1};
    QDateTime createdAt;
    QDateTime updatedAt;
};

struct MaterialSupplierDraft final {
    QString supplierName;
    QString contactName;
    QString phone;
    bool isDefault{false};
    bool isEnabled{true};
    // 交货周期（天），新增字段置于末尾以兼容聚合初始化。
    int leadDays{0};
};

// 供应商下的价格分支：电线类物料按铜价区分，普通物料铜价为 null。
struct MaterialPrice final {
    std::int64_t id{};
    std::int64_t supplierId{};
    std::optional<std::int64_t> copperPriceCents;
    std::int64_t unitPriceCents{};
    bool isDefault{false};
    bool isEnabled{true};
    std::uint32_t revision{1};
    QDateTime createdAt;
    QDateTime updatedAt;
};

struct MaterialPriceDraft final {
    std::optional<std::int64_t> copperPriceCents;
    std::int64_t unitPriceCents{};
    bool isDefault{false};
    bool isEnabled{true};
};

struct Customer final {
    std::int64_t id{};
    QString name;
    QString contactName;
    QString phone;
    QString address;
    QString notes;
    std::uint32_t revision{1};
    QDateTime createdAt;
    QDateTime updatedAt;
};

struct CustomerDraft final {
    QString name;
    QString contactName;
    QString phone;
    QString address;
    QString notes;
};

enum class RepositoryErrorCode {
    None,
    NotFound,
    RevisionConflict,
    Duplicate,
    Database,
};

struct RepositoryError final {
    RepositoryErrorCode code{RepositoryErrorCode::None};
    QString message;
};

enum class CatalogErrorCode {
    InvalidRequest,
    NotFound,
    RevisionConflict,
    DuplicateCode,
    Database,
};

struct CatalogError final {
    CatalogErrorCode code{CatalogErrorCode::InvalidRequest};
    QString message;
    QString field;
};

template <typename T>
struct CatalogResult final {
    std::optional<T> value;
    std::optional<CatalogError> error;

    bool ok() const { return value.has_value() && !error.has_value(); }
};

} // namespace manage::data
