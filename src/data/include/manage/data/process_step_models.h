#pragma once

#include <QDateTime>
#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

namespace manage::data {

// 工序库条目：记录一道生产工序所需的单人工时。
// 同一道工序可以在多个报价单中重复选用，工时以分钟为单位精确存储。
struct ProcessStep final {
    std::int64_t id{};
    QString code;
    QString name;
    // 单人完成该工序所需的工时（分钟）。
    int laborMinutes{0};
    QString description;
    bool isEnabled{true};
    std::uint32_t revision{1};
    QDateTime createdAt;
    QDateTime updatedAt;
};

struct ProcessStepDraft final {
    QString code;
    QString name;
    int laborMinutes{0};
    QString description;
    bool isEnabled{true};
};

// 工序分页结果；查询参数复用 catalog_models 中的 PageQuery。
struct ProcessStepPage final {
    std::vector<ProcessStep> items;
    std::int64_t total{};
    int page{1};
    int pageSize{20};
};

enum class ProcessStepErrorCode {
    None,
    InvalidRequest,
    NotFound,
    RevisionConflict,
    DuplicateCode,
    Database,
};

struct ProcessStepError final {
    ProcessStepErrorCode code{ProcessStepErrorCode::InvalidRequest};
    QString message;
    QString field;
};

template <typename T>
struct ProcessStepResult final {
    std::optional<T> value;
    std::optional<ProcessStepError> error;

    bool ok() const { return value.has_value() && !error.has_value(); }
};

} // namespace manage::data
