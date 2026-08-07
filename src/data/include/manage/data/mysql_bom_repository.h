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
    // 批次9：按（供应商, 铜价档）解析每条 BOM 条目的真实单价。
    // 有启用供应商的物料必须指定供应商；电线类物料指定供应商后必须输入铜价。
    BomRepositoryStatus resolveItemsPricing(
        const std::vector<BomItemInput>& items,
        std::vector<BomItemPricing>& pricing,
        QString& errorMessage
    );
    bool insertItems(qint64 bomId, const std::vector<BomItemInput>& items,
                     const std::vector<BomItemPricing>& pricing,
                     QString& errorMessage);

    QSqlDatabase database_;
};

} // namespace manage::data
