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
