// ============================================================================
// capacity_evaluator.cpp
// ============================================================================
#include "capacity_evaluator.h"
#include <algorithm>

void CapacityEvaluator::evaluate(const FarmState& farm, double& total_max, double& total_min)
{
    total_max = 0.0;
    total_min = 0.0;

    for (int i = 0; i < farm.turbine_count; ++i) {
        const auto& t = farm.turbines[i];
        double max_cap = 0.0;
        double min_cap = 0.0;

        switch (t.state) {
        case TurbineState::NORMAL:
            // 正常状态下完全可用
            max_cap = t.telemetry.available_power;
            min_cap = t.telemetry.min_tech_power;
            break;

        case TurbineState::SHADOW_RESTRICTED:
            // 阴影限制：上限受安全指数衰减，下限仍为最小技术出力
        {
            double S = t.indices.S;
            max_cap = t.telemetry.min_tech_power +
                (t.telemetry.available_power - t.telemetry.min_tech_power) * S;
            min_cap = t.telemetry.min_tech_power;
        }
        break;

        case TurbineState::RESTRICTED:
            // 保护限制：强制运行在安全功率点，不可调
        {
            double S = t.indices.S;
            double safe_pwr = t.telemetry.min_tech_power +
                (t.telemetry.available_power - t.telemetry.min_tech_power) * S;
            max_cap = safe_pwr;
            min_cap = safe_pwr;
        }
        break;

        case TurbineState::HOT_STANDBY:
        case TurbineState::FAULT:
            // 不贡献任何容量
            max_cap = 0.0;
            min_cap = 0.0;
            break;

        case TurbineState::BORROWED_REG:
            // 借调调节机组视为正常可用
            max_cap = t.telemetry.available_power;
            min_cap = t.telemetry.min_tech_power;
            break;

        default:
            break;
        }

        total_max += max_cap;
        total_min += min_cap;
    }
}