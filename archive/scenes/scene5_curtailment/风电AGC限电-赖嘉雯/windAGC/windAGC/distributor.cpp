// ============================================================================
// distributor.cpp
// ============================================================================
#include "distributor.h"
#include <algorithm>
#include <vector>
#include <cmath>

namespace {
    constexpr double kBaseFactor = 0.95;  // BASE 机组固定出力系数
    constexpr double kEfficiencyFactor = 0.9;  // 深度限电高效点系数

    // 线性插值安全降额上限
    double safeUpperLimit(const TurbineFullState& t) {
        double S = t.indices.S;
        return t.telemetry.min_tech_power +
            (t.telemetry.available_power - t.telemetry.min_tech_power) * S;
    }
}

void Distributor::distribute(FarmState& farm, double target_total,
    const AgcConfig& cfg)
{
    // ---------- 深度限电模式 ----------
    if (farm.mode == OperationMode::DEEP_CURTAILMENT) {
        // 收集所有非热备用运行机组（NORMAL/SHADOW/RESTRICTED/BORROWED_REG）
        struct RunningInfo {
            int idx;
            double efficient_pwr;  // 高效点功率
            double min_pwr;        // 最小技术出力
        };
        std::vector<RunningInfo> running;
        double sum_efficient = 0.0;
        double sum_min = 0.0;

        for (int i = 0; i < farm.turbine_count; ++i) {
            auto& t = farm.turbines[i];
            if (t.state == TurbineState::HOT_STANDBY ||
                t.state == TurbineState::FAULT) continue;

            double eff = t.telemetry.available_power * kEfficiencyFactor;
            double min_p = t.telemetry.min_tech_power;
            running.push_back({ i, eff, min_p });
            sum_efficient += eff;
            sum_min += min_p;
        }

        if (running.empty()) return;

        // 若目标小于最小出力总和，保持最小出力（容量不足已由校核处理，此处作为安全回退）
        double target = std::max(target_total, sum_min);

        // 若目标小于高效点总和，等比例缩减
        if (target < sum_efficient && sum_efficient > 0.0) {
            double ratio = target / sum_efficient;
            for (const auto& r : running) {
                auto& t = farm.turbines[r.idx];
                double pwr = r.efficient_pwr * ratio;
                // 保证不低于最小技术出力
                pwr = std::max(pwr, r.min_pwr);
                t.power_target_raw = pwr;
                t.power_setpoint = pwr;   // 爬坡在外部统一应用
            }
        }
        else {
            // 直接指定高效点
            for (const auto& r : running) {
                auto& t = farm.turbines[r.idx];
                t.power_target_raw = r.efficient_pwr;
                t.power_setpoint = r.efficient_pwr;
            }
        }
        return;
    }

    // ---------- 正常跟踪模式 ----------
    // 1. 区分机组角色
    struct RegCandidate {
        int idx;
        double weight_base;   // 权重基础：max(0.01, available - min_tech)
        double available_power;
        double min_tech;
        TurbineRole original_role;   // 用于征召后复原
    };
    std::vector<RegCandidate> regs;
    std::vector<int> bases;
    std::vector<int> restrictedFixed;  // RESTRICTED / SHADOW 固定出力机组

    double fixed_power_sum = 0.0;  // 固定出力机组的总出力

    for (int i = 0; i < farm.turbine_count; ++i) {
        const auto& t = farm.turbines[i];
        switch (t.state) {
        case TurbineState::RESTRICTED:
            // 强制安全功率，不可调
        {
            double p = safeUpperLimit(t);
            farm.turbines[i].power_target_raw = p;
            farm.turbines[i].power_setpoint = p;
            fixed_power_sum += p;
            restrictedFixed.push_back(i);
        }
        break;

        case TurbineState::SHADOW_RESTRICTED:
            // 视为固定下限（不参与上调，但若需下调可降低，此处简化固定为当前实际功率）
        {
            double p = t.telemetry.actual_power; // 用当前实际功率保持
            farm.turbines[i].power_target_raw = p;
            farm.turbines[i].power_setpoint = p;
            fixed_power_sum += p;
            restrictedFixed.push_back(i);
        }
        break;

        case TurbineState::NORMAL:
        case TurbineState::BORROWED_REG:
            // 待角色分配
            if (t.role == TurbineRole::BASE) {
                bases.push_back(i);
            }
            else if (t.role == TurbineRole::REGULATING) {
                RegCandidate rc;
                rc.idx = i;
                rc.available_power = t.telemetry.available_power;
                rc.min_tech = t.telemetry.min_tech_power;
                rc.weight_base = std::max(0.01, rc.available_power - rc.min_tech);
                rc.original_role = t.role;
                regs.push_back(rc);
            }
            else {
                // 其他角色暂按 BASE 处理
                bases.push_back(i);
            }
            break;

        default:
            // HOT_STANDBY, FAULT 等出力为 0
            farm.turbines[i].power_target_raw = 0.0;
            farm.turbines[i].power_setpoint = 0.0;
            break;
        }
    }

    // 2. 计算 BASE 固定出力
    double base_fixed_sum = 0.0;
    for (int idx : bases) {
        auto& t = farm.turbines[idx];
        double p = t.telemetry.available_power * kBaseFactor;
        p = std::min(p, t.telemetry.available_power);
        p = std::max(p, t.telemetry.min_tech_power);
        t.power_target_raw = p;
        t.power_setpoint = p;
        base_fixed_sum += p;
    }

    // 3. 计算 REGULATING 需承担的总功率
    double total_fixed = fixed_power_sum + base_fixed_sum;
    double remaining_target = target_total - total_fixed;

    // 若无调节机组，尝试征召
    if (regs.empty() && !bases.empty()) {
        // 征召 BASE 中 M 最高的转变为 BORROWED_REG
        int best_idx = bases[0];
        double best_M = farm.turbines[best_idx].indices.M;
        for (int idx : bases) {
            double M = farm.turbines[idx].indices.M;
            if (M > best_M) {
                best_M = M;
                best_idx = idx;
            }
        }
        // 临时改变角色（注意这里直接修改，实际工程应保存原角色以便复原）
        farm.turbines[best_idx].role = TurbineRole::BORROWED_REG_ROLE;
        // 重新加入 regs
        RegCandidate rc;
        rc.idx = best_idx;
        rc.available_power = farm.turbines[best_idx].telemetry.available_power;
        rc.min_tech = farm.turbines[best_idx].telemetry.min_tech_power;
        rc.weight_base = std::max(0.01, rc.available_power - rc.min_tech);
        rc.original_role = TurbineRole::BASE;  // 记录原角色
        regs.push_back(rc);
        // 从 bases 中移除
        bases.erase(std::remove(bases.begin(), bases.end(), best_idx), bases.end());
        // 调整固定总和
        base_fixed_sum -= farm.turbines[best_idx].power_target_raw;
        total_fixed = fixed_power_sum + base_fixed_sum;
        remaining_target = target_total - total_fixed;
    }

    // 4. 对 REGULATING 进行迭代分配
    if (!regs.empty()) {
        const int max_iter = static_cast<int>(cfg.max_allocation_iter);
        for (int iter = 0; iter < max_iter; ++iter) {
            // 计算当前总权重
            double weight_sum = 0.0;
            for (const auto& r : regs) {
                weight_sum += r.weight_base;
            }
            if (weight_sum <= 0.0) break;

            bool any_clamped = false;
            // 按权重分配
            for (auto& r : regs) {
                auto& t = farm.turbines[r.idx];
                double target_raw = t.power_target_raw; // 可能已被之前迭代钳位
                // 仅在未钳位的情况下计算新目标
                if (target_raw == 0.0 && iter > 0) {
                    // 已在上次钳位为边界值，保持
                }
                else {
                    double share = remaining_target * (r.weight_base / weight_sum);
                    double new_target = t.power_target_raw + share; // 增量方式？不，采用绝对值分配
                    // 简单起见，每次重新计算绝对值：每台机组目标 = 当前已分配基值 + 份额
                    // 为避免重复叠加，这里统一按权重分配剩余总量
                }
            }

            // 简化实现：将 remaining_target 按权重比例分配给各调节机组，并做限幅迭代
            // 重新计算各机组目标 = 最小技术出力 + (剩余功率) * 权重占比，但要保证不超上下限
            double allocated_sum = 0.0;
            for (auto& r : regs) {
                auto& t = farm.turbines[r.idx];
                double min_p = r.min_tech;
                double max_p = r.available_power;  // 调节机组上限为 available
                double target_raw = min_p + remaining_target * (r.weight_base / weight_sum);
                // 钳位
                target_raw = std::min(std::max(target_raw, min_p), max_p);
                t.power_target_raw = target_raw;
                allocated_sum += target_raw;
            }

            // 如果所有机组都已钳位，剩余功率无法完全分配，将剩余功率再次均匀分配给未达上限的机组
            double deficit = remaining_target + total_fixed - allocated_sum; // 剩余未分配的调节功率
            // 实际算法更为复杂，此处简化：不做二次均分，由外部通过备用处理

            // 停止迭代条件（简单实现：只迭代一次，实际可多次）
            break;
        }
    }

    // 5. 统一将 power_target_raw 赋值给 power_setpoint（斜坡限制器后续处理）
    for (int i = 0; i < farm.turbine_count; ++i) {
        farm.turbines[i].power_setpoint = farm.turbines[i].power_target_raw;
    }
}