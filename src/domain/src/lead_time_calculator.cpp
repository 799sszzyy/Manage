#include "manage/domain/lead_time_calculator.h"

#include <limits>
#include <utility>

namespace manage::domain {

LeadTimeError::LeadTimeError(LeadTimeErrorCode code, std::string message)
    : std::runtime_error(std::move(message)), code_(code) {}

LeadTimeErrorCode LeadTimeError::code() const noexcept {
    return code_;
}

int computeBomLeadDays(const std::vector<int>& materialLeadDays) {
    int maximum = 0;
    for (const auto leadDays : materialLeadDays) {
        if (leadDays < 0) {
            throw LeadTimeError(
                LeadTimeErrorCode::NegativeLeadDay,
                "material lead days must not be negative"
            );
        }
        if (leadDays > maximum) {
            maximum = leadDays;
        }
    }
    return maximum;
}

int computeEstimatedDeliveryDays(
    int bomLeadDays,
    std::int64_t processTotalMinutes,
    int laborCount,
    int minutesPerWorkday
) {
    if (bomLeadDays < 0) {
        throw LeadTimeError(
            LeadTimeErrorCode::NegativeLeadDay,
            "bom lead days must not be negative"
        );
    }
    if (processTotalMinutes < 0) {
        throw LeadTimeError(
            LeadTimeErrorCode::NegativeProcessMinutes,
            "process total minutes must not be negative"
        );
    }
    if (laborCount <= 0) {
        throw LeadTimeError(
            LeadTimeErrorCode::NonPositiveLaborCount,
            "labor count must be greater than zero"
        );
    }
    if (minutesPerWorkday <= 0) {
        throw LeadTimeError(
            LeadTimeErrorCode::NonPositiveMinutesPerWorkday,
            "minutes per workday must be greater than zero"
        );
    }

    // 全部使用 int64 中间量避免 32 位溢出：
    // 劳动人数与每日工作分钟数均为用户可见的有限输入，乘积远小于 int64 上限。
    const auto divisor =
        static_cast<std::int64_t>(laborCount) * minutesPerWorkday;
    std::int64_t laborDays = 0;
    if (processTotalMinutes > 0) {
        // 向上取整：ceil(a / b) = (a + b - 1) / b，b > 0 时安全。
        laborDays = (processTotalMinutes + divisor - 1) / divisor;
    }

    const auto total = static_cast<std::int64_t>(bomLeadDays) + laborDays;
    if (total > std::numeric_limits<int>::max()) {
        throw LeadTimeError(
            LeadTimeErrorCode::AmountOverflow,
            "estimated delivery days exceed the supported range"
        );
    }
    return static_cast<int>(total);
}

} // namespace manage::domain
