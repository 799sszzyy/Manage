#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace manage::domain {

class Money final {
public:
    static constexpr Money fromCents(std::int64_t cents) noexcept {
        return Money(cents);
    }

    constexpr std::int64_t cents() const noexcept {
        return cents_;
    }

    friend constexpr bool operator==(Money lhs, Money rhs) noexcept {
        return lhs.cents_ == rhs.cents_;
    }

    friend constexpr bool operator!=(Money lhs, Money rhs) noexcept {
        return !(lhs == rhs);
    }

private:
    explicit constexpr Money(std::int64_t cents) noexcept : cents_(cents) {}

    std::int64_t cents_{};
};

struct QuoteLineInput final {
    std::string materialCode;

    // One unit is 1,000,000 micros, so fractional quantities remain exact.
    std::int64_t quantityMicros{};

    Money unitPriceSnapshot{Money::fromCents(0)};
};

struct QuoteCalculationInput final {
    std::vector<QuoteLineInput> lines;
    Money freight{Money::fromCents(0)};
    Money otherFees{Money::fromCents(0)};

    // 100 basis points = 1%; valid range is 0 to 10,000.
    std::int32_t markupBasisPoints{};
    std::int32_t taxBasisPoints{};
};

struct QuoteLineResult final {
    std::string materialCode;
    Money subtotal{Money::fromCents(0)};
};

struct QuoteCalculationResult final {
    std::vector<QuoteLineResult> lines;
    Money materialCost{Money::fromCents(0)};
    Money priceBeforeTax{Money::fromCents(0)};
    Money taxAmount{Money::fromCents(0)};
    Money priceWithTax{Money::fromCents(0)};
};

enum class QuoteCalculationErrorCode {
    NonPositiveQuantity,
    NegativeUnitPrice,
    NegativeFreight,
    NegativeOtherFees,
    MarkupOutOfRange,
    TaxOutOfRange,
    AmountOverflow,
};

class QuoteCalculationError final : public std::runtime_error {
public:
    QuoteCalculationError(
        QuoteCalculationErrorCode code,
        std::string message,
        std::optional<std::size_t> lineIndex = std::nullopt
    );

    QuoteCalculationErrorCode code() const noexcept;
    std::optional<std::size_t> lineIndex() const noexcept;

private:
    QuoteCalculationErrorCode code_;
    std::optional<std::size_t> lineIndex_;
};

// All intermediate calculations use scaled integers. Values are rounded
// half-up to the nearest cent at each line subtotal and percentage step.
QuoteCalculationResult calculateQuote(const QuoteCalculationInput& input);

} // namespace manage::domain
