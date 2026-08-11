// ============================================================================
// rest_rotation.cpp
// ============================================================================
#include "rest_rotation.h"
#include <algorithm>
#include <vector>

RestRotationScheduler::RestRotationScheduler(const AgcConfig& config)
    : cfg_(config)
{
}

void RestRotationScheduler::scheduleStops(FarmState& farm, double stop_capacity_mw)
{
    // 1. 收集可参与停机的候选机组（状态为 NORMAL）
    std::vector<size_t> candidates;
    for (int i = 0; i < farm.turbine_count; ++i) {
        if (farm.turbines[i].state == TurbineState::NORMAL) {
            candidates.push_back(i);
        }
    }

    if (candidates.empty() || stop_capacity_mw <= 0.0) return;

    // 2. 计算每台候选机组的停机优先级得分
    //    score_stop = (1-P)*0.5 + pitch_fatigue*0.5 - hot_standby_hours*0.1
    struct StopCandidate {
        size_t idx;
        double score;
        double avail_power;   // 用于累加容量
    };
    std::vector<StopCandidate> sorted;
    sorted.reserve(candidates.size());

    for (size_t idx : candidates) {
        const auto& t = farm.turbines[idx];
        double P = t.indices.P;
        double fatigue = t.telemetry.pitch_fatigue;
        double h_hours = t.hot_standby_hours;
        double score = (1.0 - P) * 0.5 + fatigue * 0.5 - h_hours * 0.1;
        sorted.push_back({ idx, score, t.telemetry.available_power });
    }

    // 3. 按得分降序排序（高分优先停机）
    std::sort(sorted.begin(), sorted.end(),
        [](const StopCandidate& a, const StopCandidate& b) {
            return a.score > b.score;
        });

    // 4. 累加容量直到满足 stop_capacity_mw
    double accumulated = 0.0;
    for (const auto& cand : sorted) {
        if (accumulated >= stop_capacity_mw) break;

        auto& t = farm.turbines[cand.idx];
        t.state = TurbineState::HOT_STANDBY;
        t.role = TurbineRole::HOT_STANDBY_ROLE;
        t.power_setpoint = 0.0;
        // 注意：不在此处增加 hot_standby_hours，该值将在后续实时循环中累加，
        //       此处仅作为轮休公平性的历史依据。

        accumulated += cand.avail_power;
    }
}

void RestRotationScheduler::scheduleStarts(FarmState& farm, double start_capacity_mw)
{
    // 1. 收集所有当前处于热备用状态的机组
    std::vector<size_t> hot_standby_indices;
    for (int i = 0; i < farm.turbine_count; ++i) {
        if (farm.turbines[i].state == TurbineState::HOT_STANDBY) {
            hot_standby_indices.push_back(i);
        }
    }

    if (hot_standby_indices.empty() || start_capacity_mw <= 0.0) return;

    // 2. 按累计热备用时间降序排序（时间长的优先启动，实现轮休公平）
    std::sort(hot_standby_indices.begin(), hot_standby_indices.end(),
        [&](size_t a, size_t b) {
            return farm.turbines[a].hot_standby_hours >
                farm.turbines[b].hot_standby_hours;
        });

    // 3. 逐步启动机组，直到恢复所需容量
    double accumulated = 0.0;
    for (size_t idx : hot_standby_indices) {
        if (accumulated >= start_capacity_mw) break;

        auto& t = farm.turbines[idx];
        t.state = TurbineState::NORMAL;
        t.role = TurbineRole::BASE;      // 恢复后初始为基荷，后续由分类器重新分配
        t.power_setpoint = t.telemetry.min_tech_power;  // 安全起见，先给最小技术出力

        accumulated += t.telemetry.available_power;
    }
}