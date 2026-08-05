#pragma once

#include "manage/data/bom_models.h"
#include "manage/data/bom_repository.h"

#include <QString>

#include <optional>
#include <utility>

namespace manage::data {

enum class BomErrorCode {
    None,
    Validation,
    NotFound,
    Conflict,
    DuplicateCode,
    Infrastructure,
};

template<typename T>
struct BomResult final {
    std::optional<T> value;
    BomErrorCode error{BomErrorCode::None};
    QString message;

    bool ok() const noexcept { return value.has_value(); }

    static BomResult success(T result) {
        BomResult response;
        response.value = std::move(result);
        return response;
    }

    static BomResult failure(BomErrorCode code, QString errorMessage) {
        BomResult response;
        response.error = code;
        response.message = std::move(errorMessage);
        return response;
    }
};

class BomService final {
public:
    explicit BomService(BomRepository& repository);

    BomResult<BomPage> list(BomSearchQuery query);
    BomResult<BomTemplate> getById(qint64 id);
    BomResult<BomTemplate> create(NewBomTemplate command);
    BomResult<BomTemplate> update(UpdateBomTemplate command);
    BomResult<BomTemplate> setEnabled(SetBomEnabled command);
    BomResult<BomTemplate> replaceItems(ReplaceBomItems command);

private:
    QString validateMetadata(const QString& code, const QString& name,
                             const QString& description) const;
    QString validateItemShape(const std::vector<BomItemInput>& items) const;
    BomErrorCode validateMaterialReferences(
        const std::vector<BomItemInput>& items,
        QString& errorMessage
    );
    BomErrorCode mapRepositoryStatus(BomRepositoryStatus status) const;

    BomRepository& repository_;
};

} // namespace manage::data
