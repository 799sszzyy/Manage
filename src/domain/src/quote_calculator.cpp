#include "manage/domain/quote_calculator.h"

#include <limits>
#include <utility>

namespace manage::domain {
namespace {

constexpr std::int64_t kQuantityScale = 1'000'000;
constexpr std::int64_t kBasisPointScale = 10'000;

std::int64_t checkedAdd(std::int64_t lhs, std::int64_t rhs) {
    const auto maximum = std::numeric_limits<std::int64_t>::max();
    if (lhs < 0 || rhs < 0 || lhs > maximum - rhs) {
        throw std::overflow_error("non-negative amount overflow");
    }
    return lhs + rhs;
}

std::int64_t checkedMultiply(std::int64_t lhs, std::int64_t rhs) {
    const auto maximum = std::numeric_limits<std::int64_t>::max();
    if (lhs < 0 || rhs < 0 || (rhs != 0 && lhs > maximum / rhs)) {
        throw std::overflow_error("non-negative amount overflow");
    }
    return lhs * rhs;
}

std::int64_t multiplyScaledAndRound(
    std::int64_t lhs,
    std::int64_t rhs,
    std::int64_t scale
) {
    if (lhs < 0 || rhs < 0 || scale <= 0) {
        throw std::logic_error("scaled multiplication requires non-negative values");
    }

    // Decompose both operands before multiplication. This keeps every
    // intermediate value inside int64_t without relying on compiler-specific
    // 128-bit integer support.
    const auto lhsWhole = lhs / scale;
    const auto lhsRemainder = lhs % scale;
    const auto rhsWhole = rhs / scale;
    const auto rhsRemainder = rhs % scale;

    const auto whole = checkedMultiply(lhsWhole, rhs);
    const auto fractionalWhole = checkedMultiply(lhsRemainder, rhsWhole);
    const auto smallProduct = checkedMultiply(lhsRemainder, rhsRemainder);
    const auto fractionalRounded = (smallProduct + (scale / 2)) / scale;

    return checkedAdd(whole, checkedAdd(fractionalWhole, fractionalRounded));
}

[[noreturn]] void throwOverflow(std::optional<std::size_t> lineIndex) {
    throw QuoteCalculationError(
        QuoteCalculationErrorCode::AmountOverflow,
        "the calculated amount exceeds the supported range",
        lineIndex
    );
}

void validatePercentage(std::int32_t basisPoints, QuoteCalculationErrorCode code) {
    if (basisPoints < 0 || basisPoints > kBasisPointScale) {
        throw QuoteCalculationError(
            code,
            "percentage must be between 0% and 100%"
        );
    }
}

} // namespace

QuoteCalculationError::QuoteCalculationError(
    QuoteCalculationErrorCode code,
    std::string message,
    std::optional<std::size_t> lineIndex
)
    : std::runtime_error(std::move(message)),
      code_(code),
      lineIndex_(lineIndex) {}

QuoteCalculationErrorCode QuoteCalculationError::code() const noexcept {
    return code_;
}

std::optional<std::size_t> QuoteCalculationError::lineIndex() const noexcept {
    return lineIndex_;
}

QuoteCalculationResult calculateQuote(const QuoteCalculationInput& input) {
    if (input.freight.cents() < 0) {
        throw QuoteCalculationError(
            QuoteCalculationErrorCode::NegativeFreight,
            "freight must not be negative"
        );
    }
    if (input.otherFees.cents() < 0) {
        throw QuoteCalculationError(
            QuoteCalculationErrorCode::NegativeOtherFees,
            "other fees must not be negative"
        );
    }

    validatePercentage(
        input.markupBasisPoints,
        QuoteCalculationErrorCode::MarkupOutOfRange
    );
    validatePercentage(
        input.taxBasisPoints,
        QuoteCalculationErrorCode::TaxOutOfRange
    );

    QuoteCalculationResult result;
    result.lines.reserve(input.lines.size());

    std::int64_t materialCost = 0;
    for (std::size_t index = 0; index < input.lines.size(); ++index) {
        const auto& line = input.lines[index];

        if (line.quantityMicros <= 0) {
            throw QuoteCalculationError(
                QuoteCalculationErrorCode::NonPositiveQuantity,
                "material quantity must be greater than zero",
                index
            );
        }
        if (line.unitPriceSnapshot.cents() < 0) {
            throw QuoteCalculationError(
                QuoteCalculationErrorCode::NegativeUnitPrice,
                "material unit price must not be negative",
                index
            );
        }

        try {
            const auto subtotal = multiplyScaledAndRound(
                line.quantityMicros,
                line.unitPriceSnapshot.cents(),
                kQuantityScale
            );
            materialCost = checkedAdd(materialCost, subtotal);
            result.lines.push_back({line.materialCode, Money::fromCents(subtotal)});
        } catch (const std::overflow_error&) {
            throwOverflow(index);
        }
    }

    try {
        const auto base = checkedAdd(
            checkedAdd(materialCost, input.freight.cents()),
            input.otherFees.cents()
        );
        const auto markup = multiplyScaledAndRound(
            base,
            input.markupBasisPoints,
            kBasisPointScale
        );
        const auto beforeTax = checkedAdd(base, markup);
        const auto tax = multiplyScaledAndRound(
            beforeTax,
            input.taxBasisPoints,
            kBasisPointScale
        );
        const auto withTax = checkedAdd(beforeTax, tax);

        result.materialCost = Money::fromCents(materialCost);
        result.priceBeforeTax = Money::fromCents(beforeTax);
        result.taxAmount = Money::fromCents(tax);
        result.priceWithTax = Money::fromCents(withTax);
    } catch (const std::overflow_error&) {
        throwOverflow(std::nullopt);
    }

    return result;
}

} // namespace manage::domain
