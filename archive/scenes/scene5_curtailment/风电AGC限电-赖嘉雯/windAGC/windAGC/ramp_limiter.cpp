// ============================================================================
// ramp_limiter.cpp
// ============================================================================
#include "ramp_limiter.h"
#include <algorithm>

void RampLimiter::applyRamp(FarmState& farm, double dt,
    double rated_power_per_turbine)
{
    const double max_rate = 0.1 * rated_power_per_turbine;  // MW/s
    const double max_delta = max_rate * dt;

    for (int i = 0; i < farm.turbine_count; ++i) {
        auto& t = farm.turbines[i];
        double prev_sp = t.power_setpoint;          // 上一周期指令
        double target_raw = t.power_target_raw;     // 本次期望目标

        // 计算允许的变化范围
        double sp_new = prev_sp;
        if (target_raw > prev_sp) {
            sp_new = std::min(target_raw, prev_sp + max_delta);
        }
        else {
            sp_new = std::max(target_raw, prev_sp - max_delta);
        }

        t.power_setpoint = sp_new;
    }
}