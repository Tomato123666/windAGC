// ============================================================================
// classifier.cpp
// ============================================================================
#include "classifier.h"
#include <algorithm>
#include <cmath>
#include <numeric>

Classifier::Classifier(const AgcConfig& config)
    : cfg_(config)
{
}

void Classifier::classify(std::vector<TurbineFullState>& turbines,
    OperationMode mode,
    double required_reg_capacity)
{
    const double kAttenuate = cfg_.k_attenuate;

    // ---------- 深度限电模式：所有健康风机均设为 BASE ----------
    if (mode == OperationMode::DEEP_CURTAILMENT) {
        for (auto& t : turbines) {
            // 仅对当前可参与控制的机组修改角色（排除故障、热备用等）
            if (t.state != TurbineState::FAULT &&
                t.state != TurbineState::HOT_STANDBY &&
                t.state != TurbineState::BORROWED_REG)
            {
                t.role = TurbineRole::BASE;
            }
            // 其他角色保持不变（如 FAULT_ROLE 等）
        }
        return;
    }

    // ---------- 正常跟踪模式 ----------
    // 1. 筛选健康候选机组（状态允许参与功率分配）
    std::vector<size_t> candidate_indices;   // 存储原始索引
    for (size_t i = 0; i < turbines.size(); ++i) {
        const auto& t = turbines[i];
        if (t.state == TurbineState::RESTRICTED ||
            t.state == TurbineState::SHADOW_RESTRICTED ||
            t.state == TurbineState::FAULT ||
            t.state == TurbineState::HOT_STANDBY ||
            t.state == TurbineState::BORROWED_REG)
        {
            continue;  // 不可参与调节
        }
        candidate_indices.push_back(i);
    }

    // 若无健康机组，保持原有角色（实际上所有机组都不可控，无需操作）
    if (candidate_indices.empty()) {
        return;
    }

    // 2. 计算每台候选机组的评分（使用 indices 中的 M, P）
    struct CandidateInfo {
        size_t idx;
        double score_reg;
        double delta_p_up;   // 上调容量 = available_power - min_tech_power
    };
    std::vector<CandidateInfo> candidates;
    candidates.reserve(candidate_indices.size());

    double total_available = 0.0;   // 所有候选机组的总可用功率，用于自动计算所需容量

    for (size_t idx : candidate_indices) {
        const auto& t = turbines[idx];
        double M = t.indices.M;
        double P = t.indices.P;
        double score_reg = M * (1.0 + P);
        double up_cap = t.telemetry.available_power - t.telemetry.min_tech_power;
        if (up_cap < 0.0) up_cap = 0.0;
        candidates.push_back({ idx, score_reg, up_cap });
        total_available += t.telemetry.available_power;
    }

    // 3. 按 score_reg 降序排序
    std::sort(candidates.begin(), candidates.end(),
        [](const CandidateInfo& a, const CandidateInfo& b) {
            return a.score_reg > b.score_reg;
        });

    // 4. 确定所需调节容量
    double required = required_reg_capacity;
    if (required < 0.0) {
        required = total_available * 0.20;   // 默认取总可用功率的 20%
    }

    // 5. 确定调节机组数量 N_reg（至少 1 台，如果调节需求大于 0）
    size_t N_reg = 0;
    double accumulated_up = 0.0;
    for (const auto& cand : candidates) {
        accumulated_up += cand.delta_p_up;
        ++N_reg;
        if (accumulated_up >= required) {
            break;
        }
    }
    if (N_reg == 0 && !candidates.empty()) {
        N_reg = 1;   // 至少指定一台为调节机组，即使容量不足也保持机动性
    }

    // 6. 分配角色：前 N_reg 台为 REGULATING，其余候选为 BASE
    //    同时非候选机组保持原有角色，无需改动（之前未被纳入调节池）
    for (size_t i = 0; i < candidates.size(); ++i) {
        size_t t_idx = candidates[i].idx;
        if (i < N_reg) {
            turbines[t_idx].role = TurbineRole::REGULATING;
        }
        else {
            turbines[t_idx].role = TurbineRole::BASE;
        }
    }
}