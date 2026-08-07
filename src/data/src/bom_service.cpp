#include "manage/data/bom_service.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace manage::data {
namespace {

template<typename T>
BomResult<T> repositoryFailure(
    BomRepositoryStatus status,
    QString message,
    BomErrorCode mappedCode
) {
    if (message.isEmpty()) {
        switch (status) {
        case BomRepositoryStatus::NotFound:
            message = QStringLiteral("BOM template was not found");
            break;
        case BomRepositoryStatus::Conflict:
            message = QStringLiteral("BOM was changed by another operation; reload and retry");
            break;
        case BomRepositoryStatus::DuplicateCode:
            message = QStringLiteral("BOM code already exists");
            break;
        case BomRepositoryStatus::InvalidMaterial:
            message = QStringLiteral("A referenced material is missing or disabled");
            break;
        case BomRepositoryStatus::DatabaseError:
            message = QStringLiteral("Database operation failed");
            break;
        case BomRepositoryStatus::Success:
            break;
        }
    }
    return BomResult<T>::failure(mappedCode, std::move(message));
}

} // namespace

BomService::BomService(BomRepository& repository) : repository_(repository) {}

BomResult<BomPage> BomService::list(BomSearchQuery query) {
    query.search = query.search.trimmed();
    if (query.page < 1) {
        return BomResult<BomPage>::failure(
            BomErrorCode::Validation,
            QStringLiteral("page must be at least 1")
        );
    }
    if (query.pageSize < 1 || query.pageSize > 100) {
        return BomResult<BomPage>::failure(
            BomErrorCode::Validation,
            QStringLiteral("pageSize must be between 1 and 100")
        );
    }
    if (query.search.size() > 200) {
        return BomResult<BomPage>::failure(
            BomErrorCode::Validation,
            QStringLiteral("search must not exceed 200 characters")
        );
    }

    BomPage result;
    QString errorMessage;
    const auto status = repository_.list(query, result, errorMessage);
    if (status != BomRepositoryStatus::Success) {
        return repositoryFailure<BomPage>(
            status,
            std::move(errorMessage),
            mapRepositoryStatus(status)
        );
    }
    return BomResult<BomPage>::success(std::move(result));
}

BomResult<BomTemplate> BomService::getById(qint64 id) {
    if (id <= 0) {
        return BomResult<BomTemplate>::failure(
            BomErrorCode::Validation,
            QStringLiteral("BOM id must be positive")
        );
    }

    std::optional<BomTemplate> result;
    QString errorMessage;
    const auto status = repository_.getById(id, result, errorMessage);
    if (status != BomRepositoryStatus::Success || !result.has_value()) {
        const auto effectiveStatus = status == BomRepositoryStatus::Success
                                         ? BomRepositoryStatus::NotFound
                                         : status;
        return repositoryFailure<BomTemplate>(
            effectiveStatus,
            std::move(errorMessage),
            mapRepositoryStatus(effectiveStatus)
        );
    }
    return BomResult<BomTemplate>::success(std::move(*result));
}

BomResult<BomTemplate> BomService::create(NewBomTemplate command) {
    command.code = command.code.trimmed();
    command.name = command.name.trimmed();
    command.description = command.description.trimmed();

    if (const auto message = validateMetadata(
            command.code,
            command.name,
            command.description
        ); !message.isEmpty()) {
        return BomResult<BomTemplate>::failure(BomErrorCode::Validation, message);
    }
    if (const auto message = validateItemShape(command.items); !message.isEmpty()) {
        return BomResult<BomTemplate>::failure(BomErrorCode::Validation, message);
    }

    QString errorMessage;
    const auto materialError = validateMaterialReferences(command.items, errorMessage);
    if (materialError != BomErrorCode::None) {
        return BomResult<BomTemplate>::failure(materialError, errorMessage);
    }

    BomTemplate result;
    const auto status = repository_.create(command, result, errorMessage);
    if (status != BomRepositoryStatus::Success) {
        return repositoryFailure<BomTemplate>(
            status,
            std::move(errorMessage),
            mapRepositoryStatus(status)
        );
    }
    return BomResult<BomTemplate>::success(std::move(result));
}

BomResult<BomTemplate> BomService::update(UpdateBomTemplate command) {
    command.code = command.code.trimmed();
    command.name = command.name.trimmed();
    command.description = command.description.trimmed();

    if (command.id <= 0 || command.expectedRevision <= 0) {
        return BomResult<BomTemplate>::failure(
            BomErrorCode::Validation,
            QStringLiteral("BOM id and expectedRevision must be positive")
        );
    }
    if (const auto message = validateMetadata(
            command.code,
            command.name,
            command.description
        ); !message.isEmpty()) {
        return BomResult<BomTemplate>::failure(BomErrorCode::Validation, message);
    }

    BomTemplate result;
    QString errorMessage;
    const auto status = repository_.update(command, result, errorMessage);
    if (status != BomRepositoryStatus::Success) {
        return repositoryFailure<BomTemplate>(
            status,
            std::move(errorMessage),
            mapRepositoryStatus(status)
        );
    }
    return BomResult<BomTemplate>::success(std::move(result));
}

BomResult<BomTemplate> BomService::setEnabled(SetBomEnabled command) {
    if (command.id <= 0 || command.expectedRevision <= 0) {
        return BomResult<BomTemplate>::failure(
            BomErrorCode::Validation,
            QStringLiteral("BOM id and expectedRevision must be positive")
        );
    }

    BomTemplate result;
    QString errorMessage;
    const auto status = repository_.setEnabled(command, result, errorMessage);
    if (status != BomRepositoryStatus::Success) {
        return repositoryFailure<BomTemplate>(
            status,
            std::move(errorMessage),
            mapRepositoryStatus(status)
        );
    }
    return BomResult<BomTemplate>::success(std::move(result));
}

BomResult<BomTemplate> BomService::replaceItems(ReplaceBomItems command) {
    if (command.id <= 0 || command.expectedRevision <= 0) {
        return BomResult<BomTemplate>::failure(
            BomErrorCode::Validation,
            QStringLiteral("BOM id and expectedRevision must be positive")
        );
    }
    if (const auto message = validateItemShape(command.items); !message.isEmpty()) {
        return BomResult<BomTemplate>::failure(BomErrorCode::Validation, message);
    }

    QString errorMessage;
    const auto materialError = validateMaterialReferences(command.items, errorMessage);
    if (materialError != BomErrorCode::None) {
        return BomResult<BomTemplate>::failure(materialError, errorMessage);
    }

    BomTemplate result;
    const auto status = repository_.replaceItems(command, result, errorMessage);
    if (status != BomRepositoryStatus::Success) {
        return repositoryFailure<BomTemplate>(
            status,
            std::move(errorMessage),
            mapRepositoryStatus(status)
        );
    }
    return BomResult<BomTemplate>::success(std::move(result));
}

QString BomService::validateMetadata(
    const QString& code,
    const QString& name,
    const QString& description
) const {
    if (code.isEmpty() || code.size() > 64) {
        return QStringLiteral("code is required and must not exceed 64 characters");
    }
    static const QRegularExpression codePattern(
        QStringLiteral("^[A-Za-z0-9._-]+$")
    );
    if (!codePattern.match(code).hasMatch()) {
        return QStringLiteral("code may contain only letters, digits, dot, underscore and hyphen");
    }
    if (name.isEmpty() || name.size() > 200) {
        return QStringLiteral("name is required and must not exceed 200 characters");
    }
    if (description.size() > 1000) {
        return QStringLiteral("description must not exceed 1000 characters");
    }
    return {};
}

QString BomService::validateItemShape(
    const std::vector<BomItemInput>& items
) const {
    if (items.size() > 1000) {
        return QStringLiteral("a BOM cannot contain more than 1000 items");
    }

    QSet<int> lineNumbers;
    QSet<qint64> materialIds;
    for (std::size_t index = 0; index < items.size(); ++index) {
        const auto& item = items[index];
        if (item.lineNo <= 0) {
            return QStringLiteral("items[%1].lineNo must be positive").arg(index);
        }
        if (item.materialId <= 0) {
            return QStringLiteral("items[%1].materialId must be positive").arg(index);
        }
        if (item.quantityMicros <= 0) {
            return QStringLiteral("items[%1].quantityMicros must be positive").arg(index);
        }
        if (item.notes.size() > 500) {
            return QStringLiteral("items[%1].notes must not exceed 500 characters").arg(index);
        }
        if (item.materialSupplierId < 0) {
            return QStringLiteral("items[%1].materialSupplierId must not be negative").arg(index);
        }
        if (item.copperPriceCents.has_value() && *item.copperPriceCents <= 0) {
            return QStringLiteral("items[%1].copperPriceCents must be positive").arg(index);
        }
        if (lineNumbers.contains(item.lineNo)) {
            return QStringLiteral("duplicate BOM line number: %1").arg(item.lineNo);
        }
        if (materialIds.contains(item.materialId)) {
            return QStringLiteral("duplicate material in BOM: %1").arg(item.materialId);
        }
        lineNumbers.insert(item.lineNo);
        materialIds.insert(item.materialId);
    }
    return {};
}

BomErrorCode BomService::validateMaterialReferences(
    const std::vector<BomItemInput>& items,
    QString& errorMessage
) {
    if (items.empty()) {
        return BomErrorCode::None;
    }

    std::vector<qint64> ids;
    ids.reserve(items.size());
    for (const auto& item : items) {
        ids.push_back(item.materialId);
    }

    std::vector<MaterialReference> materials;
    const auto status = repository_.lookupMaterials(ids, materials, errorMessage);
    if (status != BomRepositoryStatus::Success) {
        if (errorMessage.isEmpty()) {
            errorMessage = QStringLiteral("Unable to validate BOM materials");
        }
        return mapRepositoryStatus(status);
    }

    QHash<qint64, bool> enabledById;
    for (const auto& material : materials) {
        enabledById.insert(material.id, material.isEnabled);
    }
    for (const auto id : ids) {
        if (!enabledById.contains(id)) {
            errorMessage = QStringLiteral("material %1 does not exist").arg(id);
            return BomErrorCode::Validation;
        }
        if (!enabledById.value(id)) {
            errorMessage = QStringLiteral("material %1 is disabled").arg(id);
            return BomErrorCode::Validation;
        }
    }
    return BomErrorCode::None;
}

BomErrorCode BomService::mapRepositoryStatus(BomRepositoryStatus status) const {
    switch (status) {
    case BomRepositoryStatus::Success:
        return BomErrorCode::None;
    case BomRepositoryStatus::NotFound:
        return BomErrorCode::NotFound;
    case BomRepositoryStatus::Conflict:
        return BomErrorCode::Conflict;
    case BomRepositoryStatus::DuplicateCode:
        return BomErrorCode::DuplicateCode;
    case BomRepositoryStatus::InvalidMaterial:
        return BomErrorCode::Validation;
    case BomRepositoryStatus::DatabaseError:
        return BomErrorCode::Infrastructure;
    }
    return BomErrorCode::Infrastructure;
}

} // namespace manage::data
