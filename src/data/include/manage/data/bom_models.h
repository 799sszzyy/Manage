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
