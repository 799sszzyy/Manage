#include "manage/data/mysql_quote_lifecycle.h"

#include "manage/domain/lead_time_calculator.h"
#include "manage/domain/quote_calculator.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

#include <cstdint>
#include <utility>

namespace manage::data {
namespace {

constexpr auto kQuoteDocumentColumns =
    "q.id, q.quote_number, q.customer_id, q.customer_name_snapshot, "
    "q.customer_contact_snapshot, q.customer_phone_snapshot, "
    "q.customer_address_snapshot, q.bom_template_id, q.bom_quantity_micros, "
    "q.bom_lead_days, q.labor_count, q.process_total_minutes, "
    "q.estimated_delivery_days, q.status, "
    "q.material_cost_cents, q.freight_cents, q.other_fees_cents, "
    "q.markup_basis_points, q.markup_amount_cents, "
    "q.price_before_tax_cents, q.tax_basis_points, q.tax_amount_cents, "
    "q.price_with_tax_cents, q.notes, q.source_quote_id, q.created_by, "
    "q.updated_by, q.issued_at, q.voided_at, q.revision, q.created_at, "
    "q.updated_at, q.assigned_engineer_id, q.expected_completion_at, "
    "q.engineer_submitted_at, q.estimated_delivery_at";

template<typename T>
QuoteResult<T> failure(QuoteErrorCode code, const QString& message) {
    return QuoteResult<T>::failure(code, message);
}

QuoteErrorCode sqlErrorCode(const QSqlError& error) {
    if (error.nativeErrorCode() == QStringLiteral("1062")) {
        return QuoteErrorCode::Duplicate;
    }
    if (error.nativeErrorCode() == QStringLiteral("1406")) {
        return QuoteErrorCode::Validation;
    }
    return QuoteErrorCode::Infrastructure;
}

template<typename T>
QuoteResult<T> queryFailure(const QString& operation, const QSqlQuery& query) {
    return failure<T>(
        sqlErrorCode(query.lastError()),
        QStringLiteral("%1: %2").arg(operation, query.lastError().text())
    );
}

QVariant idValue(qint64 value) {
    return QVariant::fromValue<qlonglong>(value);
}

QVariant optionalIdValue(const std::optional<qint64>& value) {
    return value.has_value()
               ? idValue(*value)
               : QVariant(QMetaType::fromType<qlonglong>());
}

QVariant optionalDateTimeValue(const std::optional<QDateTime>& value) {
    return value.has_value() && value->isValid()
               ? QVariant::fromValue<QDateTime>(*value)
               : QVariant(QMetaType::fromType<QDateTime>());
}

QString nonNull(const QString& value) {
    return value.isNull() ? QString() : value;
}

class Transaction final {
public:
    explicit Transaction(QSqlDatabase database)
        : database_(std::move(database)), active_(database_.transaction()) {}

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    ~Transaction() {
        if (active_) {
            database_.rollback();
        }
    }

    bool started() const noexcept { return active_; }

    bool commit() {
        if (!active_ || !database_.commit()) {
            return false;
        }
        active_ = false;
        return true;
    }

private:
    QSqlDatabase database_;
    bool active_{};
};

struct PreparedLine final {
    qint64 materialId{};
    QString code;
    QString name;
    QString specification;
    QString unit;
    qint64 quantityMicros{};
    qint64 unitPriceCents{};
    qint64 subtotalCents{};
    QString notes;
    // 铜价档快照（可选）：电线类物料报价时选定的铜价（元/吨）。
    std::optional<qint64> copperPriceCents;
};

// 报价工序步骤快照：名称 + 单人工时（分钟）。
struct PreparedProcessLine final {
    QString stepName;
    qint64 laborMinutes{};
};

struct PreparedDraft final {
    qint64 customerId{};
    QString customerName;
    QString customerContact;
    QString customerPhone;
    QString customerAddress;
    std::optional<qint64> bomTemplateId;
    qint64 bomQuantityMicros{1'000'000};
    // 交期快照：BOM 交期 = 组成物料最大交期；
    // 预计发货交期 = BOM 交期 + ceil(工序总工时 / 劳动人数 / 每日工作分钟)。
    int bomLeadDays{0};
    int laborCount{1};
    qint64 processTotalMinutes{0};
    int estimatedDeliveryDays{0};
    qint64 materialCostCents{};
    qint64 freightCents{};
    qint64 otherFeesCents{};
    int markupBasisPoints{};
    qint64 markupAmountCents{};
    qint64 priceBeforeTaxCents{};
    int taxBasisPoints{};
    qint64 taxAmountCents{};
    qint64 priceWithTaxCents{};
    QString notes;
    // 工程师责任制：指派工程师账号与预测 BOM 构建完成时间。
    std::optional<qint64> engineerId;
    std::optional<QDateTime> expectedCompletionAt;
    std::vector<PreparedLine> items;
    std::vector<PreparedProcessLine> processSteps;
};

QuoteResult<bool> validateActor(QSqlDatabase database, qint64 actorUserId) {
    if (actorUserId <= 0) {
        return failure<bool>(
            QuoteErrorCode::Validation,
            QStringLiteral("actorUserId must be greater than zero")
        );
    }
    QSqlQuery query(database);
    query.prepare(QStringLiteral("SELECT 1 FROM users WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), idValue(actorUserId));
    if (!query.exec()) {
        return queryFailure<bool>(QStringLiteral("unable to validate quote actor"), query);
    }
    if (!query.next()) {
        return failure<bool>(
            QuoteErrorCode::Validation,
            QStringLiteral("quote actor does not exist")
        );
    }
    return QuoteResult<bool>::success(true);
}

QuoteResult<PreparedDraft> prepareDraft(
    QSqlDatabase database,
    const QuoteDraft& draft,
    qint64 actorUserId
) {
    if (draft.customerId <= 0) {
        return failure<PreparedDraft>(
            QuoteErrorCode::Validation,
            QStringLiteral("customerId must be greater than zero")
        );
    }
    if (draft.items.empty()) {
        return failure<PreparedDraft>(
            QuoteErrorCode::Validation,
            QStringLiteral("a quote must contain at least one item")
        );
    }
    if (draft.items.size() > 1000) {
        return failure<PreparedDraft>(
            QuoteErrorCode::Validation,
            QStringLiteral("a quote cannot contain more than 1000 items")
        );
    }
    if (draft.bomQuantityMicros <= 0) {
        return failure<PreparedDraft>(
            QuoteErrorCode::Validation,
            QStringLiteral("bomQuantityMicros must be greater than zero")
        );
    }
    if (draft.laborCount < 1 || draft.laborCount > 9'999) {
        return failure<PreparedDraft>(
            QuoteErrorCode::Validation,
            QStringLiteral("laborCount must be between 1 and 9999")
        );
    }
    if (draft.processSteps.size() > 200) {
        return failure<PreparedDraft>(
            QuoteErrorCode::Validation,
            QStringLiteral("a quote cannot contain more than 200 process steps")
        );
    }
    for (std::size_t index = 0; index < draft.processSteps.size(); ++index) {
        const auto& step = draft.processSteps[index];
        if (step.stepName.trimmed().isEmpty() ||
            step.stepName.size() > 200) {
            return failure<PreparedDraft>(
                QuoteErrorCode::Validation,
                QStringLiteral("processSteps[%1].stepName must contain 1-200 characters")
                    .arg(index)
            );
        }
        if (step.laborMinutes < 0 || step.laborMinutes > 1'440) {
            return failure<PreparedDraft>(
                QuoteErrorCode::Validation,
                QStringLiteral(
                    "processSteps[%1].laborMinutes must be between 0 and 1440 minutes"
                ).arg(index)
            );
        }
    }

    const auto actor = validateActor(database, actorUserId);
    if (!actor.ok()) {
        return failure<PreparedDraft>(actor.error, actor.message);
    }

    PreparedDraft prepared;
    prepared.customerId = draft.customerId;
    prepared.bomTemplateId = draft.bomTemplateId;
    prepared.bomQuantityMicros = draft.bomQuantityMicros;
    prepared.freightCents = draft.freightCents;
    prepared.otherFeesCents = draft.otherFeesCents;
    prepared.markupBasisPoints = draft.markupBasisPoints;
    prepared.taxBasisPoints = draft.taxBasisPoints;
    prepared.notes = nonNull(draft.notes);
    // 工程师责任制字段：仅在草稿阶段允许指派/修改；发布后保持快照。
    prepared.engineerId = draft.assignedEngineerId;
    prepared.expectedCompletionAt = draft.expectedCompletionAt;
    if (draft.assignedEngineerId.has_value() && *draft.assignedEngineerId <= 0) {
        return failure<PreparedDraft>(
            QuoteErrorCode::Validation,
            QStringLiteral("assignedEngineerId must be greater than zero")
        );
    }
    if (draft.expectedCompletionAt.has_value() &&
        !draft.expectedCompletionAt->isValid()) {
        return failure<PreparedDraft>(
            QuoteErrorCode::Validation,
            QStringLiteral("expectedCompletionAt must be a valid timestamp")
        );
    }

    QSqlQuery customer(database);
    customer.prepare(QStringLiteral(
        "SELECT name, contact_name, phone, address FROM customers WHERE id = :id"
    ));
    customer.bindValue(QStringLiteral(":id"), idValue(draft.customerId));
    if (!customer.exec()) {
        return queryFailure<PreparedDraft>(
            QStringLiteral("unable to read quote customer"), customer
        );
    }
    if (!customer.next()) {
        return failure<PreparedDraft>(
            QuoteErrorCode::Validation,
            QStringLiteral("quote customer does not exist")
        );
    }
    prepared.customerName = customer.value(0).toString();
    prepared.customerContact = customer.value(1).toString();
    prepared.customerPhone = customer.value(2).toString();
    prepared.customerAddress = customer.value(3).toString();

    if (draft.bomTemplateId.has_value()) {
        if (*draft.bomTemplateId <= 0) {
            return failure<PreparedDraft>(
                QuoteErrorCode::Validation,
                QStringLiteral("bomTemplateId must be greater than zero")
            );
        }
        QSqlQuery bom(database);
        bom.prepare(QStringLiteral("SELECT 1 FROM bom_templates WHERE id = :id"));
        bom.bindValue(QStringLiteral(":id"), idValue(*draft.bomTemplateId));
        if (!bom.exec()) {
            return queryFailure<PreparedDraft>(
                QStringLiteral("unable to validate quote BOM"), bom
            );
        }
        if (!bom.next()) {
            return failure<PreparedDraft>(
                QuoteErrorCode::Validation,
                QStringLiteral("quote BOM does not exist")
            );
        }

        // BOM 交期 = 组成该 BOM 的所有物料中，默认供应商交货周期最大者。
        // 未配置供应商的物料按 0 天参与比较（不影响最大值）。
        QSqlQuery lead(database);
        lead.prepare(QStringLiteral(
            "SELECT COALESCE(MAX(s.lead_days), 0) FROM bom_items bi "
            "LEFT JOIN material_suppliers s ON s.material_id = bi.material_id "
            "AND s.is_default = TRUE AND s.is_enabled = TRUE "
            "WHERE bi.bom_template_id = :bomId"
        ));
        lead.bindValue(QStringLiteral(":bomId"), idValue(*draft.bomTemplateId));
        if (!lead.exec()) {
            return queryFailure<PreparedDraft>(
                QStringLiteral("unable to read BOM lead days"), lead
            );
        }
        if (!lead.next()) {
            return failure<PreparedDraft>(
                QuoteErrorCode::Infrastructure,
                QStringLiteral("unable to read BOM lead days")
            );
        }
        prepared.bomLeadDays = lead.value(0).toInt();
    }

    prepared.laborCount = draft.laborCount;
    prepared.processSteps.reserve(draft.processSteps.size());
    for (const auto& step : draft.processSteps) {
        prepared.processSteps.push_back({
            step.stepName.trimmed(),
            step.laborMinutes,
        });
        prepared.processTotalMinutes += step.laborMinutes;
    }

    manage::domain::QuoteCalculationInput calculation;
    calculation.freight = manage::domain::Money::fromCents(draft.freightCents);
    calculation.otherFees =
        manage::domain::Money::fromCents(draft.otherFeesCents);
    calculation.markupBasisPoints = draft.markupBasisPoints;
    calculation.taxBasisPoints = draft.taxBasisPoints;
    calculation.lines.reserve(draft.items.size());
    prepared.items.reserve(draft.items.size());

    for (const auto& input : draft.items) {
        if (input.materialId <= 0) {
            return failure<PreparedDraft>(
                QuoteErrorCode::Validation,
                QStringLiteral("materialId must be greater than zero")
            );
        }
        QSqlQuery material(database);
        material.prepare(QStringLiteral(
            "SELECT code, name, specification, unit FROM materials WHERE id = :id"
        ));
        material.bindValue(QStringLiteral(":id"), idValue(input.materialId));
        if (!material.exec()) {
            return queryFailure<PreparedDraft>(
                QStringLiteral("unable to read quote material"), material
            );
        }
        if (!material.next()) {
            return failure<PreparedDraft>(
                QuoteErrorCode::Validation,
                QStringLiteral("quote material %1 does not exist")
                    .arg(input.materialId)
            );
        }

        PreparedLine line;
        line.materialId = input.materialId;
        line.code = material.value(0).toString();
        line.name = material.value(1).toString();
        line.specification = material.value(2).toString();
        line.unit = material.value(3).toString();
        line.quantityMicros = input.quantityMicros;
        line.unitPriceCents = input.unitPriceCents;
        line.notes = nonNull(input.notes);
        line.copperPriceCents = input.copperPriceCents;
        prepared.items.push_back(std::move(line));
        calculation.lines.push_back({
            prepared.items.back().code.toStdString(),
            input.quantityMicros,
            manage::domain::Money::fromCents(input.unitPriceCents),
        });
    }

    try {
        const auto result = manage::domain::calculateQuote(calculation);
        prepared.materialCostCents = result.materialCost.cents();
        prepared.priceBeforeTaxCents = result.priceBeforeTax.cents();
        prepared.taxAmountCents = result.taxAmount.cents();
        prepared.priceWithTaxCents = result.priceWithTax.cents();
        const auto base = prepared.materialCostCents + prepared.freightCents +
                          prepared.otherFeesCents;
        prepared.markupAmountCents = prepared.priceBeforeTaxCents - base;
        for (std::size_t index = 0; index < prepared.items.size(); ++index) {
            prepared.items[index].subtotalCents = result.lines[index].subtotal.cents();
        }
    } catch (const manage::domain::QuoteCalculationError& error) {
        return failure<PreparedDraft>(
            QuoteErrorCode::Validation,
            QString::fromStdString(error.what())
        );
    }

    try {
        // 预计发货交期 = BOM 交期 + ceil(工序总工时 / 劳动人数 / 每日工作分钟)。
        prepared.estimatedDeliveryDays =
            manage::domain::computeEstimatedDeliveryDays(
                prepared.bomLeadDays,
                prepared.processTotalMinutes,
                prepared.laborCount
            );
    } catch (const manage::domain::LeadTimeError& error) {
        return failure<PreparedDraft>(
            QuoteErrorCode::Validation,
            QString::fromStdString(error.what())
        );
    }

    return QuoteResult<PreparedDraft>::success(std::move(prepared));
}

void bindPreparedHeader(
    QSqlQuery& query,
    const PreparedDraft& draft,
    qint64 actorUserId
) {
    query.bindValue(QStringLiteral(":customerId"), idValue(draft.customerId));
    query.bindValue(QStringLiteral(":customerName"), draft.customerName);
    query.bindValue(QStringLiteral(":customerContact"), draft.customerContact);
    query.bindValue(QStringLiteral(":customerPhone"), draft.customerPhone);
    query.bindValue(QStringLiteral(":customerAddress"), draft.customerAddress);
    query.bindValue(QStringLiteral(":bomTemplateId"), optionalIdValue(draft.bomTemplateId));
    query.bindValue(QStringLiteral(":bomQuantity"), idValue(draft.bomQuantityMicros));
    query.bindValue(QStringLiteral(":bomLeadDays"), draft.bomLeadDays);
    query.bindValue(QStringLiteral(":laborCount"), draft.laborCount);
    query.bindValue(QStringLiteral(":processTotalMinutes"), idValue(draft.processTotalMinutes));
    query.bindValue(QStringLiteral(":estimatedDeliveryDays"), draft.estimatedDeliveryDays);
    query.bindValue(QStringLiteral(":materialCost"), idValue(draft.materialCostCents));
    query.bindValue(QStringLiteral(":freight"), idValue(draft.freightCents));
    query.bindValue(QStringLiteral(":otherFees"), idValue(draft.otherFeesCents));
    query.bindValue(QStringLiteral(":markupRate"), draft.markupBasisPoints);
    query.bindValue(QStringLiteral(":markupAmount"), idValue(draft.markupAmountCents));
    query.bindValue(QStringLiteral(":beforeTax"), idValue(draft.priceBeforeTaxCents));
    query.bindValue(QStringLiteral(":taxRate"), draft.taxBasisPoints);
    query.bindValue(QStringLiteral(":taxAmount"), idValue(draft.taxAmountCents));
    query.bindValue(QStringLiteral(":withTax"), idValue(draft.priceWithTaxCents));
    query.bindValue(QStringLiteral(":notes"), draft.notes);
    query.bindValue(QStringLiteral(":assignedEngineerId"), optionalIdValue(draft.engineerId));
    query.bindValue(
        QStringLiteral(":expectedCompletionAt"),
        optionalDateTimeValue(draft.expectedCompletionAt)
    );
    query.bindValue(QStringLiteral(":actor"), idValue(actorUserId));
}

QuoteResult<bool> insertPreparedLines(
    QSqlDatabase database,
    qint64 quoteId,
    const std::vector<PreparedLine>& lines
) {
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "INSERT INTO quote_items (quote_id, line_no, material_id, "
        "material_code_snapshot, material_name_snapshot, "
        "specification_snapshot, unit_snapshot, quantity_micros, "
        "unit_price_cents, copper_price_cents, subtotal_cents, notes) VALUES "
        "(:quoteId, :lineNo, :materialId, :code, :name, :specification, "
        ":unit, :quantity, :unitPrice, :copperPrice, :subtotal, :notes)"
    ));
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const auto& line = lines[index];
        query.bindValue(QStringLiteral(":quoteId"), idValue(quoteId));
        query.bindValue(QStringLiteral(":lineNo"), static_cast<int>(index + 1));
        query.bindValue(QStringLiteral(":materialId"), idValue(line.materialId));
        query.bindValue(QStringLiteral(":code"), line.code);
        query.bindValue(QStringLiteral(":name"), line.name);
        query.bindValue(QStringLiteral(":specification"), line.specification);
        query.bindValue(QStringLiteral(":unit"), line.unit);
        query.bindValue(QStringLiteral(":quantity"), idValue(line.quantityMicros));
        query.bindValue(QStringLiteral(":unitPrice"), idValue(line.unitPriceCents));
        query.bindValue(
            QStringLiteral(":copperPrice"),
            line.copperPriceCents.has_value()
                ? QVariant::fromValue<qlonglong>(*line.copperPriceCents)
                : QVariant(QVariant::LongLong)
        );
        query.bindValue(QStringLiteral(":subtotal"), idValue(line.subtotalCents));
        query.bindValue(QStringLiteral(":notes"), line.notes);
        if (!query.exec()) {
            return queryFailure<bool>(
                QStringLiteral("unable to save quote item %1").arg(index + 1),
                query
            );
        }
    }
    return QuoteResult<bool>::success(true);
}

QuoteResult<bool> insertPreparedProcessItems(
    QSqlDatabase database,
    qint64 quoteId,
    const std::vector<PreparedProcessLine>& steps
) {
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "INSERT INTO quote_process_items (quote_id, line_no, "
        "step_name_snapshot, labor_minutes_snapshot) VALUES "
        "(:quoteId, :lineNo, :stepName, :laborMinutes)"
    ));
    for (std::size_t index = 0; index < steps.size(); ++index) {
        const auto& step = steps[index];
        query.bindValue(QStringLiteral(":quoteId"), idValue(quoteId));
        query.bindValue(QStringLiteral(":lineNo"), static_cast<int>(index + 1));
        query.bindValue(QStringLiteral(":stepName"), step.stepName);
        query.bindValue(QStringLiteral(":laborMinutes"), idValue(step.laborMinutes));
        if (!query.exec()) {
            return queryFailure<bool>(
                QStringLiteral("unable to save quote process step %1")
                    .arg(index + 1),
                query
            );
        }
    }
    return QuoteResult<bool>::success(true);
}

QuoteSummary readSummary(const QSqlQuery& query) {
    QuoteSummary summary;
    summary.id = query.value(0).toLongLong();
    summary.quoteNumber = query.value(1).toString();
    summary.customerId = query.value(2).toLongLong();
    summary.customerName = query.value(3).toString();
    if (!query.value(4).isNull()) {
        summary.bomTemplateId = query.value(4).toLongLong();
    }
    summary.bomQuantityMicros = query.value(5).toLongLong();
    summary.bomLeadDays = query.value(6).toInt();
    summary.laborCount = query.value(7).toInt();
    summary.processTotalMinutes = query.value(8).toLongLong();
    summary.estimatedDeliveryDays = query.value(9).toInt();
    summary.status = quoteStatusFromCode(query.value(10).toString())
                         .value_or(QuoteStatus::Draft);
    summary.priceWithTaxCents = query.value(11).toLongLong();
    summary.revision = query.value(12).toInt();
    summary.createdAt = query.value(13).toDateTime();
    summary.updatedAt = query.value(14).toDateTime();
    if (!query.value(15).isNull()) summary.assignedEngineerId = query.value(15).toLongLong();
    if (!query.value(16).isNull()) summary.expectedCompletionAt = query.value(16).toDateTime();
    if (!query.value(17).isNull()) summary.engineerSubmittedAt = query.value(17).toDateTime();
    if (!query.value(18).isNull()) summary.estimatedDeliveryAt = query.value(18).toDateTime();
    return summary;
}

QuoteDocument readDocumentHeader(const QSqlQuery& query) {
    QuoteDocument document;
    document.summary.id = query.value(0).toLongLong();
    document.summary.quoteNumber = query.value(1).toString();
    document.summary.customerId = query.value(2).toLongLong();
    document.summary.customerName = query.value(3).toString();
    document.customerContact = query.value(4).toString();
    document.customerPhone = query.value(5).toString();
    document.customerAddress = query.value(6).toString();
    if (!query.value(7).isNull()) {
        document.summary.bomTemplateId = query.value(7).toLongLong();
    }
    document.summary.bomQuantityMicros = query.value(8).toLongLong();
    document.summary.bomLeadDays = query.value(9).toInt();
    document.summary.laborCount = query.value(10).toInt();
    document.summary.processTotalMinutes = query.value(11).toLongLong();
    document.summary.estimatedDeliveryDays = query.value(12).toInt();
    document.summary.status = quoteStatusFromCode(query.value(13).toString())
                                  .value_or(QuoteStatus::Draft);
    document.materialCostCents = query.value(14).toLongLong();
    document.freightCents = query.value(15).toLongLong();
    document.otherFeesCents = query.value(16).toLongLong();
    document.markupBasisPoints = query.value(17).toInt();
    document.markupAmountCents = query.value(18).toLongLong();
    document.priceBeforeTaxCents = query.value(19).toLongLong();
    document.taxBasisPoints = query.value(20).toInt();
    document.taxAmountCents = query.value(21).toLongLong();
    document.priceWithTaxCents = query.value(22).toLongLong();
    document.summary.priceWithTaxCents = document.priceWithTaxCents;
    document.notes = query.value(23).toString();
    if (!query.value(24).isNull()) {
        document.sourceQuoteId = query.value(24).toLongLong();
    }
    document.createdBy = query.value(25).toLongLong();
    document.updatedBy = query.value(26).toLongLong();
    document.issuedAt = query.value(27).toDateTime();
    document.voidedAt = query.value(28).toDateTime();
    document.summary.revision = query.value(29).toInt();
    document.summary.createdAt = query.value(30).toDateTime();
    document.summary.updatedAt = query.value(31).toDateTime();
    if (!query.value(32).isNull()) document.summary.assignedEngineerId = query.value(32).toLongLong();
    if (!query.value(33).isNull()) document.summary.expectedCompletionAt = query.value(33).toDateTime();
    if (!query.value(34).isNull()) document.summary.engineerSubmittedAt = query.value(34).toDateTime();
    if (!query.value(35).isNull()) document.summary.estimatedDeliveryAt = query.value(35).toDateTime();
    return document;
}

QuoteResult<QuoteDocument> loadDocument(QSqlDatabase database, qint64 id) {
    QSqlQuery query(database);
    query.prepare(QStringLiteral("SELECT %1 FROM quotes q WHERE q.id = :id")
                      .arg(QString::fromLatin1(kQuoteDocumentColumns)));
    query.bindValue(QStringLiteral(":id"), idValue(id));
    if (!query.exec()) {
        return queryFailure<QuoteDocument>(QStringLiteral("unable to read quote"), query);
    }
    if (!query.next()) {
        return failure<QuoteDocument>(
            QuoteErrorCode::NotFound,
            QStringLiteral("quote not found")
        );
    }
    auto document = readDocumentHeader(query);

    QSqlQuery items(database);
    items.prepare(QStringLiteral(
        "SELECT id, line_no, material_id, material_code_snapshot, "
        "material_name_snapshot, specification_snapshot, unit_snapshot, "
        "quantity_micros, unit_price_cents, copper_price_cents, "
        "subtotal_cents, notes "
        "FROM quote_items WHERE quote_id = :quoteId ORDER BY line_no"
    ));
    items.bindValue(QStringLiteral(":quoteId"), idValue(id));
    if (!items.exec()) {
        return queryFailure<QuoteDocument>(
            QStringLiteral("unable to read quote items"), items
        );
    }
    while (items.next()) {
        QuoteItemSnapshot item;
        item.id = items.value(0).toLongLong();
        item.lineNo = items.value(1).toInt();
        item.materialId = items.value(2).toLongLong();
        item.materialCode = items.value(3).toString();
        item.materialName = items.value(4).toString();
        item.specification = items.value(5).toString();
        item.unit = items.value(6).toString();
        item.quantityMicros = items.value(7).toLongLong();
        item.unitPriceCents = items.value(8).toLongLong();
        if (items.value(9).isNull()) {
            item.copperPriceCents.reset();
        } else {
            item.copperPriceCents = items.value(9).toLongLong();
        }
        item.subtotalCents = items.value(10).toLongLong();
        item.notes = items.value(11).toString();
        document.items.push_back(std::move(item));
    }

    QSqlQuery process(database);
    process.prepare(QStringLiteral(
        "SELECT id, line_no, step_name_snapshot, labor_minutes_snapshot "
        "FROM quote_process_items WHERE quote_id = :quoteId ORDER BY line_no"
    ));
    process.bindValue(QStringLiteral(":quoteId"), idValue(id));
    if (!process.exec()) {
        return queryFailure<QuoteDocument>(
            QStringLiteral("unable to read quote process steps"), process
        );
    }
    while (process.next()) {
        QuoteProcessSnapshot step;
        step.id = process.value(0).toLongLong();
        step.lineNo = process.value(1).toInt();
        step.stepName = process.value(2).toString();
        step.laborMinutes = process.value(3).toLongLong();
        document.processSteps.push_back(std::move(step));
    }
    return QuoteResult<QuoteDocument>::success(std::move(document));
}

QuoteResult<QuoteDocument> commitAndLoad(
    Transaction& transaction,
    QSqlDatabase database,
    qint64 id
) {
    const auto document = loadDocument(database, id);
    if (!document.ok()) {
        return document;
    }
    if (!transaction.commit()) {
        return failure<QuoteDocument>(
            QuoteErrorCode::Infrastructure,
            QStringLiteral("unable to commit quote transaction: %1")
                .arg(database.lastError().text())
        );
    }
    return document;
}

QuoteResult<bool> lockQuoteState(
    QSqlDatabase database,
    qint64 id,
    QuoteStatus& status,
    int& revision
) {
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "SELECT status, revision FROM quotes WHERE id = :id FOR UPDATE"
    ));
    query.bindValue(QStringLiteral(":id"), idValue(id));
    if (!query.exec()) {
        return queryFailure<bool>(QStringLiteral("unable to lock quote"), query);
    }
    if (!query.next()) {
        return failure<bool>(QuoteErrorCode::NotFound, QStringLiteral("quote not found"));
    }
    const auto parsedStatus = quoteStatusFromCode(query.value(0).toString());
    if (!parsedStatus.has_value()) {
        return failure<bool>(
            QuoteErrorCode::Infrastructure,
            QStringLiteral("quote contains an unsupported status")
        );
    }
    status = *parsedStatus;
    revision = query.value(1).toInt();
    return QuoteResult<bool>::success(true);
}

} // namespace

MySqlQuoteLifecycle::MySqlQuoteLifecycle(QSqlDatabase database)
    : database_(std::move(database)) {}

QuoteResult<QuotePage> MySqlQuoteLifecycle::list(QuoteSearchQuery request) {
    if (request.page < 1 || request.pageSize < 1 || request.pageSize > 100) {
        return failure<QuotePage>(
            QuoteErrorCode::Validation,
            QStringLiteral("page must be positive and pageSize must be between 1 and 100")
        );
    }
    if (request.customerId.has_value() && *request.customerId <= 0) {
        return failure<QuotePage>(
            QuoteErrorCode::Validation,
            QStringLiteral("customerId must be greater than zero")
        );
    }

    Transaction transaction(database_);
    if (!transaction.started()) {
        return failure<QuotePage>(
            QuoteErrorCode::Infrastructure,
            QStringLiteral("unable to start quote list transaction: %1")
                .arg(database_.lastError().text())
        );
    }

    QStringList conditions;
    if (!request.search.trimmed().isEmpty()) {
        conditions.append(QStringLiteral(
            "(LOCATE(:searchNumber, quote_number) > 0 OR "
            "LOCATE(:searchCustomer, customer_name_snapshot) > 0)"
        ));
    }
    if (request.status.has_value()) {
        conditions.append(QStringLiteral("status = :status"));
    }
    if (request.customerId.has_value()) {
        conditions.append(QStringLiteral("customer_id = :customerId"));
    }
    const auto where = conditions.isEmpty()
                           ? QString()
                           : QStringLiteral(" WHERE %1").arg(
                                 conditions.join(QStringLiteral(" AND "))
                             );
    const auto bindFilters = [&](QSqlQuery& query) {
        if (!request.search.trimmed().isEmpty()) {
            query.bindValue(QStringLiteral(":searchNumber"), request.search.trimmed());
            query.bindValue(QStringLiteral(":searchCustomer"), request.search.trimmed());
        }
        if (request.status.has_value()) {
            query.bindValue(QStringLiteral(":status"), quoteStatusCode(*request.status));
        }
        if (request.customerId.has_value()) {
            query.bindValue(QStringLiteral(":customerId"), idValue(*request.customerId));
        }
    };

    QSqlQuery count(database_);
    count.prepare(QStringLiteral("SELECT COUNT(*) FROM quotes%1").arg(where));
    bindFilters(count);
    if (!count.exec() || !count.next()) {
        return queryFailure<QuotePage>(QStringLiteral("unable to count quotes"), count);
    }

    QuotePage page;
    page.total = count.value(0).toLongLong();
    page.page = request.page;
    page.pageSize = request.pageSize;
    const auto offset = (static_cast<qint64>(request.page) - 1) * request.pageSize;
    QSqlQuery items(database_);
    items.prepare(QStringLiteral(
        "SELECT id, quote_number, customer_id, customer_name_snapshot, "
        "bom_template_id, bom_quantity_micros, bom_lead_days, labor_count, "
        "process_total_minutes, estimated_delivery_days, status, "
        "price_with_tax_cents, revision, created_at, "
        "updated_at, assigned_engineer_id, expected_completion_at, "
        "engineer_submitted_at, estimated_delivery_at "
        "FROM quotes%1 ORDER BY id DESC LIMIT %2 OFFSET %3"
    ).arg(where).arg(request.pageSize).arg(offset));
    bindFilters(items);
    if (!items.exec()) {
        return queryFailure<QuotePage>(QStringLiteral("unable to list quotes"), items);
    }
    while (items.next()) {
        page.items.push_back(readSummary(items));
    }
    if (!transaction.commit()) {
        return failure<QuotePage>(
            QuoteErrorCode::Infrastructure,
            QStringLiteral("unable to finish quote list transaction: %1")
                .arg(database_.lastError().text())
        );
    }
    return QuoteResult<QuotePage>::success(std::move(page));
}

QuoteResult<QuoteDocument> MySqlQuoteLifecycle::getById(qint64 id) {
    if (id <= 0) {
        return failure<QuoteDocument>(
            QuoteErrorCode::Validation,
            QStringLiteral("quote id must be greater than zero")
        );
    }
    Transaction transaction(database_);
    if (!transaction.started()) {
        return failure<QuoteDocument>(
            QuoteErrorCode::Infrastructure,
            QStringLiteral("unable to start quote detail transaction: %1")
                .arg(database_.lastError().text())
        );
    }
    const auto document = loadDocument(database_, id);
    if (!document.ok()) {
        return document;
    }
    if (!transaction.commit()) {
        return failure<QuoteDocument>(
            QuoteErrorCode::Infrastructure,
            QStringLiteral("unable to finish quote detail transaction: %1")
                .arg(database_.lastError().text())
        );
    }
    return document;
}

QuoteResult<QuoteDocument> MySqlQuoteLifecycle::create(CreateQuoteCommand command) {
    // prepareDraft reads customer/material data and freezes their display and
    // price fields. The transaction keeps the header, snapshots and totals as
    // one aggregate even if a later line insert fails.
    Transaction transaction(database_);
    if (!transaction.started()) {
        return failure<QuoteDocument>(
            QuoteErrorCode::Infrastructure,
            QStringLiteral("unable to start quote transaction: %1")
                .arg(database_.lastError().text())
        );
    }
    const auto prepared = prepareDraft(database_, command.draft, command.actorUserId);
    if (!prepared.ok()) {
        return failure<QuoteDocument>(prepared.error, prepared.message);
    }

    QSqlQuery insert(database_);
    insert.prepare(QStringLiteral(
        "INSERT INTO quotes (quote_number, customer_id, customer_name_snapshot, "
        "customer_contact_snapshot, customer_phone_snapshot, "
        "customer_address_snapshot, bom_template_id, bom_quantity_micros, "
        "bom_lead_days, labor_count, process_total_minutes, "
        "estimated_delivery_days, status, material_cost_cents, "
        "freight_cents, other_fees_cents, markup_basis_points, markup_amount_cents, "
        "price_before_tax_cents, tax_basis_points, tax_amount_cents, "
        "price_with_tax_cents, notes, source_quote_id, "
        "assigned_engineer_id, expected_completion_at, created_by, updated_by) "
        "VALUES (:temporaryNumber, :customerId, :customerName, :customerContact, "
        ":customerPhone, :customerAddress, :bomTemplateId, :bomQuantity, "
        ":bomLeadDays, :laborCount, :processTotalMinutes, "
        ":estimatedDeliveryDays, 'draft', :materialCost, "
        ":freight, :otherFees, :markupRate, :markupAmount, :beforeTax, :taxRate, "
        ":taxAmount, :withTax, :notes, NULL, "
        ":assignedEngineerId, :expectedCompletionAt, :actor, :actor)"
    ));
    bindPreparedHeader(insert, *prepared.value, command.actorUserId);
    insert.bindValue(
        QStringLiteral(":temporaryNumber"),
        QStringLiteral("TMP-%1").arg(
            QUuid::createUuid().toString(QUuid::WithoutBraces)
        )
    );
    if (!insert.exec()) {
        return queryFailure<QuoteDocument>(QStringLiteral("unable to create quote"), insert);
    }
    const auto quoteId = insert.lastInsertId().toLongLong();

    QSqlQuery number(database_);
    number.prepare(QStringLiteral(
        "UPDATE quotes SET quote_number = CONCAT('Q-', "
        "DATE_FORMAT(CURRENT_DATE(), '%Y%m%d'), '-', id) WHERE id = :id"
    ));
    number.bindValue(QStringLiteral(":id"), idValue(quoteId));
    if (!number.exec() || number.numRowsAffected() != 1) {
        return queryFailure<QuoteDocument>(
            QStringLiteral("unable to assign quote number"), number
        );
    }
    // 交期日期 = 创建日期 + 预计交期天数（SQL DATE_ADD 精确计算）。
    QSqlQuery deliveryDate(database_);
    deliveryDate.prepare(QStringLiteral(
        "UPDATE quotes SET estimated_delivery_at = "
        "DATE_ADD(created_at, INTERVAL estimated_delivery_days DAY) WHERE id = :id"
    ));
    deliveryDate.bindValue(QStringLiteral(":id"), idValue(quoteId));
    if (!deliveryDate.exec()) {
        return queryFailure<QuoteDocument>(
            QStringLiteral("unable to set estimated delivery date"), deliveryDate
        );
    }
    const auto lines = insertPreparedLines(database_, quoteId, prepared.value->items);
    if (!lines.ok()) {
        return failure<QuoteDocument>(lines.error, lines.message);
    }
    const auto processSteps =
        insertPreparedProcessItems(database_, quoteId, prepared.value->processSteps);
    if (!processSteps.ok()) {
        return failure<QuoteDocument>(processSteps.error, processSteps.message);
    }
    return commitAndLoad(transaction, database_, quoteId);
}

QuoteResult<QuoteDocument> MySqlQuoteLifecycle::update(UpdateQuoteCommand command) {
    if (command.id <= 0 || command.expectedRevision <= 0) {
        return failure<QuoteDocument>(
            QuoteErrorCode::Validation,
            QStringLiteral("quote id and revision must be greater than zero")
        );
    }
    Transaction transaction(database_);
    if (!transaction.started()) {
        return failure<QuoteDocument>(
            QuoteErrorCode::Infrastructure,
            QStringLiteral("unable to start quote transaction: %1")
                .arg(database_.lastError().text())
        );
    }
    QuoteStatus currentStatus;
    int currentRevision = 0;
    // Lock + revision comparison implements optimistic concurrency: a second
    // editor must reload instead of overwriting the first editor's changes.
    const auto locked = lockQuoteState(database_, command.id, currentStatus, currentRevision);
    if (!locked.ok()) {
        return failure<QuoteDocument>(locked.error, locked.message);
    }
    if (currentStatus != QuoteStatus::Draft) {
        return failure<QuoteDocument>(
            QuoteErrorCode::Conflict,
            QStringLiteral("only a draft quote can be updated")
        );
    }
    if (currentRevision != command.expectedRevision) {
        return failure<QuoteDocument>(
            QuoteErrorCode::Conflict,
            QStringLiteral("quote revision conflict")
        );
    }
    const auto prepared = prepareDraft(database_, command.draft, command.actorUserId);
    if (!prepared.ok()) {
        return failure<QuoteDocument>(prepared.error, prepared.message);
    }

    QSqlQuery updateQuery(database_);
    updateQuery.prepare(QStringLiteral(
        "UPDATE quotes SET customer_id = :customerId, "
        "customer_name_snapshot = :customerName, "
        "customer_contact_snapshot = :customerContact, "
        "customer_phone_snapshot = :customerPhone, "
        "customer_address_snapshot = :customerAddress, "
        "bom_template_id = :bomTemplateId, bom_quantity_micros = :bomQuantity, "
        "bom_lead_days = :bomLeadDays, labor_count = :laborCount, "
        "process_total_minutes = :processTotalMinutes, "
        "estimated_delivery_days = :estimatedDeliveryDays, "
        "estimated_delivery_at = DATE_ADD(created_at, INTERVAL :estimatedDeliveryDays DAY), "
        "material_cost_cents = :materialCost, "
        "freight_cents = :freight, other_fees_cents = :otherFees, "
        "markup_basis_points = :markupRate, markup_amount_cents = :markupAmount, "
        "price_before_tax_cents = :beforeTax, tax_basis_points = :taxRate, "
        "tax_amount_cents = :taxAmount, price_with_tax_cents = :withTax, "
        "assigned_engineer_id = :assignedEngineerId, "
        "expected_completion_at = :expectedCompletionAt, "
        "notes = :notes, updated_by = :actor, revision = revision + 1 "
        "WHERE id = :id AND status = 'draft' AND revision = :revision"
    ));
    bindPreparedHeader(updateQuery, *prepared.value, command.actorUserId);
    updateQuery.bindValue(QStringLiteral(":id"), idValue(command.id));
    updateQuery.bindValue(QStringLiteral(":revision"), command.expectedRevision);
    if (!updateQuery.exec()) {
        return queryFailure<QuoteDocument>(
            QStringLiteral("unable to update quote"), updateQuery
        );
    }
    if (updateQuery.numRowsAffected() != 1) {
        return failure<QuoteDocument>(
            QuoteErrorCode::Conflict,
            QStringLiteral("quote revision conflict")
        );
    }

    QSqlQuery removeItems(database_);
    removeItems.prepare(QStringLiteral("DELETE FROM quote_items WHERE quote_id = :id"));
    removeItems.bindValue(QStringLiteral(":id"), idValue(command.id));
    if (!removeItems.exec()) {
        return queryFailure<QuoteDocument>(
            QStringLiteral("unable to replace quote items"), removeItems
        );
    }
    const auto lines = insertPreparedLines(database_, command.id, prepared.value->items);
    if (!lines.ok()) {
        return failure<QuoteDocument>(lines.error, lines.message);
    }

    QSqlQuery removeProcess(database_);
    removeProcess.prepare(QStringLiteral(
        "DELETE FROM quote_process_items WHERE quote_id = :id"
    ));
    removeProcess.bindValue(QStringLiteral(":id"), idValue(command.id));
    if (!removeProcess.exec()) {
        return queryFailure<QuoteDocument>(
            QStringLiteral("unable to replace quote process steps"), removeProcess
        );
    }
    const auto processSteps =
        insertPreparedProcessItems(database_, command.id, prepared.value->processSteps);
    if (!processSteps.ok()) {
        return failure<QuoteDocument>(processSteps.error, processSteps.message);
    }
    return commitAndLoad(transaction, database_, command.id);
}

QuoteResult<QuoteDocument> MySqlQuoteLifecycle::changeStatus(
    ChangeQuoteStatusCommand command
) {
    if (command.id <= 0 || command.expectedRevision <= 0) {
        return failure<QuoteDocument>(
            QuoteErrorCode::Validation,
            QStringLiteral("quote id and revision must be greater than zero")
        );
    }
    if (command.targetStatus != QuoteStatus::Issued &&
        command.targetStatus != QuoteStatus::Void) {
        return failure<QuoteDocument>(
            QuoteErrorCode::Validation,
            QStringLiteral("target status must be issued or void")
        );
    }
    Transaction transaction(database_);
    if (!transaction.started()) {
        return failure<QuoteDocument>(
            QuoteErrorCode::Infrastructure,
            QStringLiteral("unable to start quote transaction: %1")
                .arg(database_.lastError().text())
        );
    }
    const auto actor = validateActor(database_, command.actorUserId);
    if (!actor.ok()) {
        return failure<QuoteDocument>(actor.error, actor.message);
    }
    QuoteStatus currentStatus;
    int currentRevision = 0;
    const auto locked = lockQuoteState(database_, command.id, currentStatus, currentRevision);
    if (!locked.ok()) {
        return failure<QuoteDocument>(locked.error, locked.message);
    }
    if (currentRevision != command.expectedRevision) {
        return failure<QuoteDocument>(
            QuoteErrorCode::Conflict,
            QStringLiteral("quote revision conflict")
        );
    }
    // There is deliberately no approval state in this product. A quote moves
    // forward only through draft -> issued -> void.
    const auto legal =
        (currentStatus == QuoteStatus::Draft &&
         command.targetStatus == QuoteStatus::Issued) ||
        (currentStatus == QuoteStatus::Issued &&
         command.targetStatus == QuoteStatus::Void);
    if (!legal) {
        return failure<QuoteDocument>(
            QuoteErrorCode::InvalidTransition,
            QStringLiteral("illegal quote status transition from %1 to %2")
                .arg(quoteStatusCode(currentStatus), quoteStatusCode(command.targetStatus))
        );
    }

    QSqlQuery updateQuery(database_);
    if (command.targetStatus == QuoteStatus::Issued) {
        updateQuery.prepare(QStringLiteral(
            "UPDATE quotes SET status = 'issued', issued_at = CURRENT_TIMESTAMP(6), "
            "engineer_submitted_at = IF(assigned_engineer_id IS NOT NULL, "
            "COALESCE(engineer_submitted_at, CURRENT_TIMESTAMP(6)), "
            "engineer_submitted_at), "
            "voided_at = NULL, updated_by = :actor, revision = revision + 1 "
            "WHERE id = :id AND revision = :revision AND status = 'draft'"
        ));
    } else {
        updateQuery.prepare(QStringLiteral(
            "UPDATE quotes SET status = 'void', voided_at = CURRENT_TIMESTAMP(6), "
            "updated_by = :actor, revision = revision + 1 "
            "WHERE id = :id AND revision = :revision AND status = 'issued'"
        ));
    }
    updateQuery.bindValue(QStringLiteral(":actor"), idValue(command.actorUserId));
    updateQuery.bindValue(QStringLiteral(":id"), idValue(command.id));
    updateQuery.bindValue(QStringLiteral(":revision"), command.expectedRevision);
    if (!updateQuery.exec()) {
        return queryFailure<QuoteDocument>(
            QStringLiteral("unable to change quote status"), updateQuery
        );
    }
    if (updateQuery.numRowsAffected() != 1) {
        return failure<QuoteDocument>(
            QuoteErrorCode::Conflict,
            QStringLiteral("quote changed while its status was being updated")
        );
    }
    return commitAndLoad(transaction, database_, command.id);
}

QuoteResult<QuoteDocument> MySqlQuoteLifecycle::clone(CloneQuoteCommand command) {
    if (command.sourceId <= 0) {
        return failure<QuoteDocument>(
            QuoteErrorCode::Validation,
            QStringLiteral("source quote id must be greater than zero")
        );
    }
    Transaction transaction(database_);
    if (!transaction.started()) {
        return failure<QuoteDocument>(
            QuoteErrorCode::Infrastructure,
            QStringLiteral("unable to start quote transaction: %1")
                .arg(database_.lastError().text())
        );
    }
    const auto actor = validateActor(database_, command.actorUserId);
    if (!actor.ok()) {
        return failure<QuoteDocument>(actor.error, actor.message);
    }
    const auto source = loadDocument(database_, command.sourceId);
    if (!source.ok()) {
        return failure<QuoteDocument>(source.error, source.message);
    }

    QSqlQuery insert(database_);
    insert.prepare(QStringLiteral(
        "INSERT INTO quotes (quote_number, customer_id, customer_name_snapshot, "
        "customer_contact_snapshot, customer_phone_snapshot, "
        "customer_address_snapshot, bom_template_id, bom_quantity_micros, "
        "bom_lead_days, labor_count, process_total_minutes, "
        "estimated_delivery_days, status, material_cost_cents, "
        "freight_cents, other_fees_cents, markup_basis_points, markup_amount_cents, "
        "price_before_tax_cents, tax_basis_points, tax_amount_cents, "
        "price_with_tax_cents, notes, source_quote_id, "
        "assigned_engineer_id, expected_completion_at, created_by, updated_by) "
        "VALUES (:temporaryNumber, :customerId, :customerName, :customerContact, "
        ":customerPhone, :customerAddress, :bomTemplateId, :bomQuantity, "
        ":bomLeadDays, :laborCount, :processTotalMinutes, "
        ":estimatedDeliveryDays, 'draft', :materialCost, "
        ":freight, :otherFees, :markupRate, :markupAmount, :beforeTax, :taxRate, "
        ":taxAmount, :withTax, :notes, :sourceId, "
        ":assignedEngineerId, :expectedCompletionAt, :actor, :actor)"
    ));
    const auto& original = *source.value;
    insert.bindValue(
        QStringLiteral(":temporaryNumber"),
        QStringLiteral("TMP-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces))
    );
    insert.bindValue(QStringLiteral(":customerId"), idValue(original.summary.customerId));
    insert.bindValue(QStringLiteral(":customerName"), original.summary.customerName);
    insert.bindValue(QStringLiteral(":customerContact"), original.customerContact);
    insert.bindValue(QStringLiteral(":customerPhone"), original.customerPhone);
    insert.bindValue(QStringLiteral(":customerAddress"), original.customerAddress);
    insert.bindValue(
        QStringLiteral(":bomTemplateId"),
        optionalIdValue(original.summary.bomTemplateId)
    );
    insert.bindValue(QStringLiteral(":bomQuantity"), idValue(original.summary.bomQuantityMicros));
    insert.bindValue(QStringLiteral(":bomLeadDays"), original.summary.bomLeadDays);
    insert.bindValue(QStringLiteral(":laborCount"), original.summary.laborCount);
    insert.bindValue(
        QStringLiteral(":processTotalMinutes"),
        idValue(original.summary.processTotalMinutes)
    );
    insert.bindValue(
        QStringLiteral(":estimatedDeliveryDays"),
        original.summary.estimatedDeliveryDays
    );
    insert.bindValue(QStringLiteral(":materialCost"), idValue(original.materialCostCents));
    insert.bindValue(QStringLiteral(":freight"), idValue(original.freightCents));
    insert.bindValue(QStringLiteral(":otherFees"), idValue(original.otherFeesCents));
    insert.bindValue(QStringLiteral(":markupRate"), original.markupBasisPoints);
    insert.bindValue(QStringLiteral(":markupAmount"), idValue(original.markupAmountCents));
    insert.bindValue(QStringLiteral(":beforeTax"), idValue(original.priceBeforeTaxCents));
    insert.bindValue(QStringLiteral(":taxRate"), original.taxBasisPoints);
    insert.bindValue(QStringLiteral(":taxAmount"), idValue(original.taxAmountCents));
    insert.bindValue(QStringLiteral(":withTax"), idValue(original.priceWithTaxCents));
    insert.bindValue(QStringLiteral(":notes"), original.notes);
    insert.bindValue(QStringLiteral(":sourceId"), idValue(original.summary.id));
    // 克隆的新草稿保留工程师指派与预测时间，但提交时间为空（新任务未提交）。
    insert.bindValue(
        QStringLiteral(":assignedEngineerId"),
        optionalIdValue(original.summary.assignedEngineerId)
    );
    insert.bindValue(
        QStringLiteral(":expectedCompletionAt"),
        optionalDateTimeValue(original.summary.expectedCompletionAt)
    );
    insert.bindValue(QStringLiteral(":actor"), idValue(command.actorUserId));
    if (!insert.exec()) {
        return queryFailure<QuoteDocument>(QStringLiteral("unable to clone quote"), insert);
    }
    const auto quoteId = insert.lastInsertId().toLongLong();

    QSqlQuery number(database_);
    number.prepare(QStringLiteral(
        "UPDATE quotes SET quote_number = CONCAT('Q-', "
        "DATE_FORMAT(CURRENT_DATE(), '%Y%m%d'), '-', id) WHERE id = :id"
    ));
    number.bindValue(QStringLiteral(":id"), idValue(quoteId));
    if (!number.exec() || number.numRowsAffected() != 1) {
        return queryFailure<QuoteDocument>(
            QStringLiteral("unable to assign cloned quote number"), number
        );
    }
    // 克隆的新草稿同样计算交期日期。
    QSqlQuery cloneDeliveryDate(database_);
    cloneDeliveryDate.prepare(QStringLiteral(
        "UPDATE quotes SET estimated_delivery_at = "
        "DATE_ADD(created_at, INTERVAL estimated_delivery_days DAY) WHERE id = :id"
    ));
    cloneDeliveryDate.bindValue(QStringLiteral(":id"), idValue(quoteId));
    if (!cloneDeliveryDate.exec()) {
        return queryFailure<QuoteDocument>(
            QStringLiteral("unable to set cloned delivery date"), cloneDeliveryDate
        );
    }

    std::vector<PreparedLine> copiedLines;
    copiedLines.reserve(original.items.size());
    for (const auto& item : original.items) {
        copiedLines.push_back({
            item.materialId,
            item.materialCode,
            item.materialName,
            item.specification,
            item.unit,
            item.quantityMicros,
            item.unitPriceCents,
            item.subtotalCents,
            item.notes,
            item.copperPriceCents,
        });
    }
    const auto lines = insertPreparedLines(database_, quoteId, copiedLines);
    if (!lines.ok()) {
        return failure<QuoteDocument>(lines.error, lines.message);
    }

    std::vector<PreparedProcessLine> copiedSteps;
    copiedSteps.reserve(original.processSteps.size());
    for (const auto& step : original.processSteps) {
        copiedSteps.push_back({step.stepName, step.laborMinutes});
    }
    const auto processSteps =
        insertPreparedProcessItems(database_, quoteId, copiedSteps);
    if (!processSteps.ok()) {
        return failure<QuoteDocument>(processSteps.error, processSteps.message);
    }
    return commitAndLoad(transaction, database_, quoteId);
}

QuoteResult<bool> MySqlQuoteLifecycle::deleteDraft(
    DeleteDraftQuoteCommand command
) {
    if (command.id <= 0 || command.expectedRevision <= 0) {
        return failure<bool>(
            QuoteErrorCode::Validation,
            QStringLiteral("quote id and revision must be greater than zero")
        );
    }
    Transaction transaction(database_);
    if (!transaction.started()) {
        return failure<bool>(
            QuoteErrorCode::Infrastructure,
            QStringLiteral("unable to start quote transaction: %1")
                .arg(database_.lastError().text())
        );
    }
    const auto actor = validateActor(database_, command.actorUserId);
    if (!actor.ok()) {
        return actor;
    }
    QuoteStatus currentStatus;
    int currentRevision = 0;
    const auto locked = lockQuoteState(database_, command.id, currentStatus, currentRevision);
    if (!locked.ok()) {
        return locked;
    }
    if (currentRevision != command.expectedRevision) {
        return failure<bool>(
            QuoteErrorCode::Conflict,
            QStringLiteral("quote revision conflict")
        );
    }
    if (currentStatus != QuoteStatus::Draft) {
        return failure<bool>(
            QuoteErrorCode::Conflict,
            QStringLiteral("only a draft quote can be deleted")
        );
    }

    QSqlQuery remove(database_);
    remove.prepare(QStringLiteral(
        "DELETE FROM quotes WHERE id = :id AND revision = :revision "
        "AND status = 'draft'"
    ));
    remove.bindValue(QStringLiteral(":id"), idValue(command.id));
    remove.bindValue(QStringLiteral(":revision"), command.expectedRevision);
    if (!remove.exec()) {
        return queryFailure<bool>(QStringLiteral("unable to delete draft quote"), remove);
    }
    if (remove.numRowsAffected() != 1) {
        return failure<bool>(
            QuoteErrorCode::Conflict,
            QStringLiteral("quote changed while it was being deleted")
        );
    }
    if (!transaction.commit()) {
        return failure<bool>(
            QuoteErrorCode::Infrastructure,
            QStringLiteral("unable to commit quote deletion: %1")
                .arg(database_.lastError().text())
        );
    }
    return QuoteResult<bool>::success(true);
}

} // namespace manage::data
