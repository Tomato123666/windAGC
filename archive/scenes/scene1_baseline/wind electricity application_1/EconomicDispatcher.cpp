#include "EconomicDispatcher.h"
#include "Turbine.h"
#include "Utils.h"
#include <cmath>
#include <algorithm>

std::vector<double> EconomicDispatcher::dispatch(double total_power_target, const std::vector<std::shared_ptr<Turbine>>& turbines) {
    int n = static_cast<int>(turbines.size());
    std::vector<double> commands(n, 0.0);

    if (n == 0) return commands;

    double total_available = 0.0, total_min_power = 0.0;
    std::vector<double> max_powers(n), min_powers(n);
    std::vector<bool> available(n);

    for (int i = 0; i < n; ++i) {
        available[i] = turbines[i]->isAvailable();
        if (available[i]) {
            max_powers[i] = turbines[i]->getMaxAvailablePower();
            min_powers[i] = turbines[i]->getMinPower();
            total_available += max_powers[i];
            total_min_power += min_powers[i];
        } else {
            max_powers[i] = 0.0;
            min_powers[i] = 0.0;
        }
    }

    if (total_available <= 1e-3) return commands;

    // 限制目标在可调范围
    total_power_target = Utils::clamp(total_power_target, total_min_power, total_available);

    // ============================================================
    // 改进的经济分配算法：
    // 按可用容量加权，同时考虑效率因子。
    // 高效率机组承担更多功率，低效率机组少出力。
    // 类似于等微增率法的简化实现（PPT Slide 8/9）
    // ============================================================

    // 计算每台机组的"调节权重" = 可用容量 × 效率因子
    std::vector<double> weights(n, 0.0);
    double total_weight = 0.0;

    for (int i = 0; i < n; ++i) {
        if (!available[i]) continue;
        // 效率因子基于当前出力点：越接近额定功率效率越高
        double current_pu = turbines[i]->getActualPower() / std::max(max_powers[i], 0.01);
        // 风机在70%~90%额定功率区间效率最优
        double efficiency_factor;
        if (current_pu < 0.1) {
            efficiency_factor = 0.8;  // 低负载效率偏低
        } else if (current_pu < 0.7) {
            efficiency_factor = 0.9 + (current_pu - 0.1) * 0.2; // 过渡区
        } else if (current_pu <= 0.9) {
            efficiency_factor = 1.0;  // 最优区间
        } else {
            efficiency_factor = 0.95; // 接近满载略有下降
        }
        weights[i] = max_powers[i] * efficiency_factor;
        total_weight += weights[i];
    }

    // 先分配最小功率
    double remaining = total_power_target - total_min_power;

    for (int i = 0; i < n; ++i) {
        if (!available[i]) continue;
        commands[i] = min_powers[i];
    }

    // 按权重分配剩余功率
    if (total_weight > 1e-6) {
        for (int i = 0; i < n; ++i) {
            if (!available[i]) continue;
            double share = (weights[i] / total_weight) * remaining;
            commands[i] += share;
            // 保证不超出单机上下限
            commands[i] = Utils::clamp(commands[i], min_powers[i], max_powers[i]);
        }
    }

    // 修正：如果因限幅导致功率不平衡，迭代调整
    double sum_commands = 0.0;
    for (int i = 0; i < n; ++i) sum_commands += commands[i];

    if (std::fabs(sum_commands - total_power_target) > 0.01 && total_weight > 1e-6) {
        // 简单比例修正
        double correction = total_power_target / std::max(sum_commands, 0.01);
        for (int i = 0; i < n; ++i) {
            if (!available[i]) continue;
            commands[i] *= correction;
            commands[i] = Utils::clamp(commands[i], min_powers[i], max_powers[i]);
        }
    }

    return commands;
}
