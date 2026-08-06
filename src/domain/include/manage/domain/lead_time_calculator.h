#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace manage::domain {

enum class LeadTimeErrorCode {
    NegativeLeadDay,
    NonPositiveLaborCount,
    NonPositiveMinutesPerWorkday,
    NegativeProcessMinutes,
    AmountOverflow,
};

class LeadTimeError final : public std::runtime_error {
public:
    LeadTimeError(LeadTimeErrorCode code, std::string message);

    LeadTimeErrorCode code() const noexcept;

private:
    LeadTimeErrorCode code_;
};

// BOM 交期（天）= 组成该 BOM 的所有物料交期中的最大值。
// 空 BOM 没有物料交期贡献，返回 0。
int computeBomLeadDays(const std::vector<int>& materialLeadDays);

// 预计发货交期（天）= BOM 交期 + ceil(工序总工时分钟 / 劳动人数 / 每日工作分钟)。
// 工序总工时按"单人完成全部工序"计，除以劳动人数得到并行生产后的工作天数，
// 再向上取整保证对客户承诺的发货时间只保守不激进。
// minutesPerWorkday 默认 480（每日 8 小时工作制）。
int computeEstimatedDeliveryDays(
    int bomLeadDays,
    std::int64_t processTotalMinutes,
    int laborCount,
    int minutesPerWorkday = 480
);

} // namespace manage::domain
