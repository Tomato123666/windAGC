// ============================================================================
// ramp_limiter.h
// ============================================================================
#ifndef RAMP_LIMITER_H
#define RAMP_LIMITER_H

#include "agc_types.h"

/**
 * @class RampLimiter
 * @brief 爬坡速率限制器
 *
 * 对每台风机的功率设定值施加爬坡速率约束，防止指令突变。
 * 假设上下坡速率统一为额定功率的 10%/秒。
 */
class RampLimiter {
public:
    /**
     * @brief 应用爬坡限制
     * @param farm 全场状态（修改 power_setpoint 字段）
     * @param dt   距上次调用的时间步长 (秒)
     * @param rated_power_per_turbine 单机额定功率 (MW)，默认 2.0
     */
    static void applyRamp(FarmState& farm, double dt,
        double rated_power_per_turbine = 2.0);
};

#endif // RAMP_LIMITER_H