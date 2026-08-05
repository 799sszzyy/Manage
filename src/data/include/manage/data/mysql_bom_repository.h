#pragma once

#include "manage/data/bom_repository.h"

#include <QSqlDatabase>

namespace manage::data {

class MySqlBomRepository final : public BomRepository {
public:
    explicit MySqlBomRepository(QSqlDatabase database);

    BomRepositoryStatus list(
        const BomSearchQuery& query,
        BomPage& result,
        QString& errorMessage
    ) override;
    BomRepositoryStatus getById(
        qint64 id,
        std::optional<BomTemplate>& result,
        QString& errorMessage
    ) override;
    BomRepositoryStatus lookupMaterials(
        const std::vector<qint64>& ids,
        std::vector<MaterialReference>& result,
        QString& errorMessage
    ) override;
    BomRepositoryStatus create(
        const NewBomTemplate& command,
        BomTemplate& result,
        QString& errorMessage
    ) override;
    BomRepositoryStatus update(
        const UpdateBomTemplate& command,
        BomTemplate& result,
        QString& errorMessage
    ) override;
    BomRepositoryStatus setEnabled(
        const SetBomEnabled& command,
        BomTemplate& result,
        QString& errorMessage
    ) override;
    BomRepositoryStatus replaceItems(
        const ReplaceBomItems& command,
        BomTemplate& result,
        QString& errorMessage
    ) override;

private:
    BomRepositoryStatus load(qint64 id, BomTemplate& result, QString& errorMessage);
    BomRepositoryStatus classifyWriteError(const QString& nativeCode,
                                            const QString& message) const;
    BomRepositoryStatus statusAfterMiss(qint64 id, QString& errorMessage);
    BomRepositoryStatus lockAndValidateMaterials(
        const std::vector<BomItemInput>& items,
        QString& errorMessage
    );
    bool insertItems(qint64 bomId, const std::vector<BomItemInput>& items,
                     QString& errorMessage);

    QSqlDatabase database_;
};

} // namespace manage::data
