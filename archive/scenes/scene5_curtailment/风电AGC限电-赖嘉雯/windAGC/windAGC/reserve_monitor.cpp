// ============================================================================
// reserve_monitor.cpp
// ============================================================================
#include "reserve_monitor.h"

namespace {
    // 单机额定功率 (MW)，工程中应从机组参数表获取，此处硬编码为典型值
    constexpr double kRatedPower = 2.0;
}

void ReserveMonitor::compute(const FarmState& farm, const AgcConfig& cfg,
    double& reserve_up, double& reserve_down)
{
    reserve_up = 0.0;
    reserve_down = 0.0;

    for (int i = 0; i < farm.turbine_count; ++i) {
        const auto& t = farm.turbines[i];
        double sp = t.power_setpoint;       // 当前下发指令
        double min_p = t.telemetry.min_tech_power;
        double avail = t.telemetry.available_power;

        switch (t.state) {
        case TurbineState::NORMAL:
        case TurbineState::BORROWED_REG:
            // 正常调节机组：上调至物理最大，下调至最小技术出力
            reserve_up += (avail - sp);
            reserve_down += (sp - min_p);
            break;

        case TurbineState::SHADOW_RESTRICTED: {
            // 阴影限制：上调空间受安全上限约束（S 指数线性插值）
            double S = t.indices.S;
            double safe_upper = min_p + (avail - min_p) * S;
            reserve_up += (safe_upper - sp);
            reserve_down += (sp - min_p);
            break;
        }

        case TurbineState::RESTRICTED: {
            // 保护限制：指令已被固定为安全上限，上调空间为0
            // 下调空间理论上为 (sp - min_p)，但实际运行中禁止下调以免风险，
            // 此处仍按公式统计，备用监视器仅提供信息，是否使用由调度决定。
            reserve_down += (sp - min_p);
            break;
        }

        case TurbineState::HOT_STANDBY:
            // 热备用：上调容量 = 额定功率 × 可用系数
            reserve_up += kRatedPower * cfg.ready_factor;
            // 无下调能力
            break;

        case TurbineState::FAULT:
            // 故障停机，无贡献
            break;

        default:
            break;
        }
    }
}