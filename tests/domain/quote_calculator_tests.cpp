#include "manage/domain/quote_calculator.h"

#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using manage::domain::Money;
using manage::domain::QuoteCalculationError;
using manage::domain::QuoteCalculationErrorCode;
using manage::domain::QuoteCalculationInput;
using manage::domain::QuoteLineInput;
using manage::domain::calculateQuote;

class TestFailure final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw TestFailure(message);
    }
}

void requireCents(Money actual, std::int64_t expected, const std::string& field) {
    if (actual.cents() != expected) {
        throw TestFailure(
            field + ": expected " + std::to_string(expected) +
            " cents, got " + std::to_string(actual.cents())
        );
    }
}

template <typename Action>
void requireError(
    Action&& action,
    QuoteCalculationErrorCode expectedCode,
    std::optional<std::size_t> expectedLine = std::nullopt
) {
    try {
        std::forward<Action>(action)();
    } catch (const QuoteCalculationError& error) {
        require(error.code() == expectedCode, "unexpected error code");
        require(error.lineIndex() == expectedLine, "unexpected error line index");
        return;
    }
    throw TestFailure("expected QuoteCalculationError");
}

QuoteCalculationInput validInput() {
    QuoteCalculationInput input;
    input.lines = {
        QuoteLineInput{"MAT-001", 2'500'000, Money::fromCents(1'234)},
        QuoteLineInput{"MAT-002", 1'000'000, Money::fromCents(500)},
    };
    input.freight = Money::fromCents(1'000);
    input.otherFees = Money::fromCents(200);
    input.markupBasisPoints = 2'000;
    input.taxBasisPoints = 1'300;
    return input;
}

void calculatesQuoteUsingSnapshotsAndFixedPointMath() {
    const auto result = calculateQuote(validInput());

    require(result.lines.size() == 2, "two line results expected");
    requireCents(result.lines[0].subtotal, 3'085, "first line subtotal");
    requireCents(result.lines[1].subtotal, 500, "second line subtotal");
    requireCents(result.materialCost, 3'585, "material cost");
    requireCents(result.priceBeforeTax, 5'742, "price before tax");
    requireCents(result.taxAmount, 746, "tax amount");
    requireCents(result.priceWithTax, 6'488, "price with tax");
}

void calculatesTheUserManualExample() {
    QuoteCalculationInput input;
    input.lines = {
        // Ten M8 bolts at CNY 1.20 each. Quantities are stored in millionths
        // and money in cents so the calculation never uses floating point.
        QuoteLineInput{"DEMO-M8", 10'000'000, Money::fromCents(120)},
    };
    input.freight = Money::fromCents(500);
    input.markupBasisPoints = 1'000;
    input.taxBasisPoints = 1'300;

    const auto result = calculateQuote(input);
    requireCents(result.materialCost, 1'200, "manual example material cost");
    requireCents(result.priceBeforeTax, 1'870, "manual example price before tax");
    requireCents(result.taxAmount, 243, "manual example tax");
    requireCents(result.priceWithTax, 2'113, "manual example total");
}

void roundsHalfUpToTheNearestCent() {
    QuoteCalculationInput input;
    input.lines = {
        QuoteLineInput{"HALF-CENT", 500'000, Money::fromCents(1)},
    };

    const auto result = calculateQuote(input);
    requireCents(result.lines[0].subtotal, 1, "rounded line subtotal");
    requireCents(result.materialCost, 1, "rounded material cost");
}

void acceptsPercentageBoundaries() {
    auto input = validInput();
    input.markupBasisPoints = 10'000;
    input.taxBasisPoints = 10'000;

    const auto result = calculateQuote(input);
    requireCents(result.priceBeforeTax, 9'570, "100% markup");
    requireCents(result.priceWithTax, 19'140, "100% tax");
}

void rejectsInvalidLineValues() {
    auto input = validInput();
    input.lines[0].quantityMicros = 0;
    requireError(
        [&] { calculateQuote(input); },
        QuoteCalculationErrorCode::NonPositiveQuantity,
        0
    );

    input = validInput();
    input.lines[1].unitPriceSnapshot = Money::fromCents(-1);
    requireError(
        [&] { calculateQuote(input); },
        QuoteCalculationErrorCode::NegativeUnitPrice,
        1
    );
}

void rejectsInvalidFeesAndPercentages() {
    auto input = validInput();
    input.freight = Money::fromCents(-1);
    requireError(
        [&] { calculateQuote(input); },
        QuoteCalculationErrorCode::NegativeFreight
    );

    input = validInput();
    input.otherFees = Money::fromCents(-1);
    requireError(
        [&] { calculateQuote(input); },
        QuoteCalculationErrorCode::NegativeOtherFees
    );

    input = validInput();
    input.markupBasisPoints = 10'001;
    requireError(
        [&] { calculateQuote(input); },
        QuoteCalculationErrorCode::MarkupOutOfRange
    );

    input = validInput();
    input.taxBasisPoints = -1;
    requireError(
        [&] { calculateQuote(input); },
        QuoteCalculationErrorCode::TaxOutOfRange
    );
}

void reportsOverflowWithoutUndefinedBehavior() {
    QuoteCalculationInput input;
    input.lines = {
        QuoteLineInput{
            "TOO-LARGE",
            2'000'000,
            Money::fromCents(std::numeric_limits<std::int64_t>::max())
        },
    };

    requireError(
        [&] { calculateQuote(input); },
        QuoteCalculationErrorCode::AmountOverflow,
        0
    );
}

} // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"calculates quote", calculatesQuoteUsingSnapshotsAndFixedPointMath},
        {"calculates user manual example", calculatesTheUserManualExample},
        {"rounds half up", roundsHalfUpToTheNearestCent},
        {"accepts percentage boundaries", acceptsPercentageBoundaries},
        {"rejects invalid lines", rejectsInvalidLineValues},
        {"rejects invalid fees and percentages", rejectsInvalidFeesAndPercentages},
        {"reports overflow", reportsOverflowWithoutUndefinedBehavior},
    };

    std::size_t passed = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            ++passed;
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
        }
    }

    std::cout << passed << "/" << tests.size() << " tests passed\n";
    return passed == tests.size() ? EXIT_SUCCESS : EXIT_FAILURE;
}
