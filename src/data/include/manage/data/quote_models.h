#pragma once

#include <QDateTime>
#include <QString>
#include <QtGlobal>

#include <optional>
#include <utility>
#include <vector>

namespace manage::data {

enum class QuoteStatus {
    Draft,
    Issued,
    Void,
};

inline QString quoteStatusCode(QuoteStatus status) {
    switch (status) {
    case QuoteStatus::Draft:
        return QStringLiteral("draft");
    case QuoteStatus::Issued:
        return QStringLiteral("issued");
    case QuoteStatus::Void:
        return QStringLiteral("void");
    }
    return QStringLiteral("draft");
}

inline std::optional<QuoteStatus> quoteStatusFromCode(const QString& code) {
    if (code == QStringLiteral("draft")) {
        return QuoteStatus::Draft;
    }
    if (code == QStringLiteral("issued")) {
        return QuoteStatus::Issued;
    }
    if (code == QStringLiteral("void")) {
        return QuoteStatus::Void;
    }
    return std::nullopt;
}

struct QuoteLineInput final {
    qint64 materialId{};
    qint64 quantityMicros{};
    qint64 unitPriceCents{};
    QString notes;
};

// 报价单上的一道工序步骤：名称快照 + 单人工时（分钟）。
struct QuoteProcessInput final {
    QString stepName;
    qint64 laborMinutes{};
};

struct QuoteDraft final {
    qint64 customerId{};
    std::optional<qint64> bomTemplateId;
    // Fixed-point BOM sales quantity: 1'000'000 means one BOM.
    qint64 bomQuantityMicros{1'000'000};
    // 产品劳动人数：工序总工时除以该人数得到并行生产的工作天数。
    int laborCount{1};
    // 手动选用的工序步骤（可来自工序库，也可直接录入）。
    std::vector<QuoteProcessInput> processSteps;
    // 工程师责任制：销售指派的工程师账号（NULL 表示尚未指派）。
    std::optional<qint64> assignedEngineerId;
    // 工程师责任制：销售预测的 BOM 构建完成时间（deadline）。
    std::optional<QDateTime> expectedCompletionAt;
    qint64 freightCents{};
    qint64 otherFeesCents{};
    int markupBasisPoints{};
    int taxBasisPoints{};
    QString notes;
    std::vector<QuoteLineInput> items;
};

struct QuoteItemSnapshot final {
    qint64 id{};
    int lineNo{};
    qint64 materialId{};
    QString materialCode;
    QString materialName;
    QString specification;
    QString unit;
    qint64 quantityMicros{};
    qint64 unitPriceCents{};
    qint64 subtotalCents{};
    QString notes;
};

// 报价单上已冻结的工序步骤快照。
struct QuoteProcessSnapshot final {
    qint64 id{};
    int lineNo{};
    QString stepName;
    qint64 laborMinutes{};
};

struct QuoteSummary final {
    qint64 id{};
    QString quoteNumber;
    qint64 customerId{};
    QString customerName;
    std::optional<qint64> bomTemplateId;
    qint64 bomQuantityMicros{1'000'000};
    // 快照：BOM 交期（天）、劳动人数、工序总工时（单人分钟）、预计发货交期（天）。
    int bomLeadDays{0};
    int laborCount{1};
    qint64 processTotalMinutes{0};
    int estimatedDeliveryDays{0};
    // 交期日期 = 创建日期 + estimatedDeliveryDays（由 SQL DATE_ADD 精确计算）。
    std::optional<QDateTime> estimatedDeliveryAt;
    QuoteStatus status{QuoteStatus::Draft};
    qint64 priceWithTaxCents{};
    int revision{1};
    QDateTime createdAt;
    QDateTime updatedAt;
    // 工程师责任制：指派工程师、预测完成时间、工程师实际提交时间。
    std::optional<qint64> assignedEngineerId;
    std::optional<QDateTime> expectedCompletionAt;
    std::optional<QDateTime> engineerSubmittedAt;
};

struct QuoteDocument final {
    QuoteSummary summary;
    QString customerContact;
    QString customerPhone;
    QString customerAddress;
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
    std::optional<qint64> sourceQuoteId;
    qint64 createdBy{};
    qint64 updatedBy{};
    QDateTime issuedAt;
    QDateTime voidedAt;
    std::vector<QuoteItemSnapshot> items;
    // 已冻结的工序步骤明细（供报价单展示）。
    std::vector<QuoteProcessSnapshot> processSteps;
};

struct QuotePage final {
    std::vector<QuoteSummary> items;
    qint64 total{};
    int page{1};
    int pageSize{20};
};

struct QuoteSearchQuery final {
    int page{1};
    int pageSize{20};
    QString search;
    std::optional<QuoteStatus> status;
    std::optional<qint64> customerId;
};

struct CreateQuoteCommand final {
    QuoteDraft draft;
    qint64 actorUserId{};
};

struct UpdateQuoteCommand final {
    qint64 id{};
    int expectedRevision{};
    QuoteDraft draft;
    qint64 actorUserId{};
};

struct ChangeQuoteStatusCommand final {
    qint64 id{};
    int expectedRevision{};
    QuoteStatus targetStatus{QuoteStatus::Issued};
    qint64 actorUserId{};
};

struct CloneQuoteCommand final {
    qint64 sourceId{};
    qint64 actorUserId{};
};

struct DeleteDraftQuoteCommand final {
    qint64 id{};
    int expectedRevision{};
    qint64 actorUserId{};
};

enum class QuoteErrorCode {
    None,
    Validation,
    NotFound,
    Conflict,
    Duplicate,
    InvalidTransition,
    Infrastructure,
};

template<typename T>
struct QuoteResult final {
    std::optional<T> value;
    QuoteErrorCode error{QuoteErrorCode::None};
    QString message;

    bool ok() const noexcept { return value.has_value(); }

    static QuoteResult success(T result) {
        QuoteResult response;
        response.value = std::move(result);
        return response;
    }

    static QuoteResult failure(QuoteErrorCode code, QString errorMessage) {
        QuoteResult response;
        response.error = code;
        response.message = std::move(errorMessage);
        return response;
    }
};

} // namespace manage::data
