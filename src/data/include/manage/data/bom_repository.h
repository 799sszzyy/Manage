#pragma once

#include "manage/data/bom_models.h"

#include <QString>

#include <optional>
#include <vector>

namespace manage::data {

enum class BomRepositoryStatus {
    Success,
    NotFound,
    Conflict,
    DuplicateCode,
    InvalidMaterial,
    DatabaseError,
};

class BomRepository {
public:
    virtual ~BomRepository() = default;

    virtual BomRepositoryStatus list(
        const BomSearchQuery& query,
        BomPage& result,
        QString& errorMessage
    ) = 0;
    virtual BomRepositoryStatus getById(
        qint64 id,
        std::optional<BomTemplate>& result,
        QString& errorMessage
    ) = 0;
    virtual BomRepositoryStatus lookupMaterials(
        const std::vector<qint64>& ids,
        std::vector<MaterialReference>& result,
        QString& errorMessage
    ) = 0;
    virtual BomRepositoryStatus create(
        const NewBomTemplate& command,
        BomTemplate& result,
        QString& errorMessage
    ) = 0;
    virtual BomRepositoryStatus update(
        const UpdateBomTemplate& command,
        BomTemplate& result,
        QString& errorMessage
    ) = 0;
    virtual BomRepositoryStatus setEnabled(
        const SetBomEnabled& command,
        BomTemplate& result,
        QString& errorMessage
    ) = 0;
    virtual BomRepositoryStatus replaceItems(
        const ReplaceBomItems& command,
        BomTemplate& result,
        QString& errorMessage
    ) = 0;
};

} // namespace manage::data
