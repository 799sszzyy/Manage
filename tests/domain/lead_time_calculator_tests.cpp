#include "manage/domain/lead_time_calculator.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

class TestFailure final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw TestFailure(message);
    }
}

void bomLeadDaysTakesMaximum() {
    using manage::domain::computeBomLeadDays;
    require(computeBomLeadDays({}) == 0, "empty bom has no lead days");
    require(computeBomLeadDays({3}) == 3, "single material lead days");
    require(computeBomLeadDays({3, 5, 2}) == 5, "maximum of material lead days");
    require(computeBomLeadDays({0, 0}) == 0, "zero lead days stay zero");
    require(computeBomLeadDays({7, 3, 9, 1}) == 9, "maximum with many entries");
}

void bomLeadDaysRejectsNegative() {
    using manage::domain::computeBomLeadDays;
    using manage::domain::LeadTimeError;
    using manage::domain::LeadTimeErrorCode;
    bool thrown = false;
    try {
        computeBomLeadDays({2, -1});
    } catch (const LeadTimeError& error) {
        thrown = true;
        require(
            error.code() == LeadTimeErrorCode::NegativeLeadDay,
            "negative lead day error code"
        );
    }
    require(thrown, "negative material lead days must throw");
}

void deliveryDaysAddsLaborDaysRoundedUp() {
    using manage::domain::computeEstimatedDeliveryDays;
    // 每日工作 480 分钟（8 小时）。
    require(
        computeEstimatedDeliveryDays(10, 0, 1) == 10,
        "no process work keeps bom lead days"
    );
    require(
        computeEstimatedDeliveryDays(0, 480, 1) == 1,
        "one workday of single-person work"
    );
    require(
        computeEstimatedDeliveryDays(5, 960, 1) == 7,
        "two workdays add to bom lead days"
    );
    require(
        computeEstimatedDeliveryDays(0, 480, 2) == 1,
        "two workers halve the workload but still round up"
    );
    require(
        computeEstimatedDeliveryDays(3, 240, 2) == 4,
        "fractional workday rounds up to one day"
    );
    require(
        computeEstimatedDeliveryDays(0, 1, 999) == 1,
        "tiny workload still takes at least one day"
    );
    require(
        computeEstimatedDeliveryDays(2, 9600, 10) == 4,
        "ten workers finish ten workdays of work in one day"
    );
}

void deliveryDaysValidatesInputs() {
    using manage::domain::computeEstimatedDeliveryDays;
    using manage::domain::LeadTimeError;
    using manage::domain::LeadTimeErrorCode;

    const auto expectError = [](std::int64_t minutes, int labor, int workday) {
        try {
            computeEstimatedDeliveryDays(0, minutes, labor, workday);
        } catch (const LeadTimeError&) {
            return;
        }
        throw TestFailure("expected LeadTimeError");
    };

    expectError(0, 0, 480);
    expectError(0, -3, 480);
    expectError(0, 1, 0);
    expectError(0, 1, -480);
    expectError(-1, 1, 480);
}

} // namespace

int main() {
    try {
        bomLeadDaysTakesMaximum();
        bomLeadDaysRejectsNegative();
        deliveryDaysAddsLaborDaysRoundedUp();
        deliveryDaysValidatesInputs();
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << std::endl;
        return 1;
    }
    std::cout << "lead_time_calculator tests passed" << std::endl;
    return 0;
}
