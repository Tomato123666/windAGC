// ============================================================================
// state_machine.cpp
// ============================================================================
#include "state_machine.h"

TurbineStateMachine::TurbineStateMachine(const AgcConfig& config)
    : cfg_(config), healthy_duration_sec_(0.0)
{
}

TurbineState TurbineStateMachine::update(TurbineState current,
    double S, double dt,
    bool& stateChanged)
{
    stateChanged = false;

    // 只处理受安全指数驱动的状态，其余状态保持不变，不累积健康时间
    if (current != TurbineState::NORMAL &&
        current != TurbineState::SHADOW_RESTRICTED &&
        current != TurbineState::RESTRICTED)
    {
        // 未在S驱动状态内，清空健康累计，直接返回原状态
        healthy_duration_sec_ = 0.0;
        return current;
    }

    const double warn_thr = cfg_.S_warn_thr;       // 预警阈值
    const double force_thr = cfg_.S_force_thr;      // 强制保护阈值
    const double recover_thr = warn_thr + 0.1;      // 恢复阈值（带滞环）
    const double confirm_time = 60.0;               // 恢复确认时间 (秒)

    switch (current) {
    case TurbineState::NORMAL:
        // 正常 → 阴影限制（立即触发）
        if (S < warn_thr) {
            healthy_duration_sec_ = 0.0;
            stateChanged = true;
            return TurbineState::SHADOW_RESTRICTED;
        }
        // 保持正常，健康计时器不累积（仅从受限恢复时累积）
        healthy_duration_sec_ = 0.0;
        break;

    case TurbineState::SHADOW_RESTRICTED:
        // 阴影限制 → 保护限制（进一步恶化）
        if (S < force_thr) {
            healthy_duration_sec_ = 0.0;
            stateChanged = true;
            return TurbineState::RESTRICTED;
        }
        // 尝试恢复到正常
        if (S > recover_thr) {
            healthy_duration_sec_ += dt;
            if (healthy_duration_sec_ >= confirm_time) {
                healthy_duration_sec_ = 0.0;
                stateChanged = true;
                return TurbineState::NORMAL;
            }
        }
        else {
            healthy_duration_sec_ = 0.0;  // 不满足恢复条件，计时归零
        }
        break;

    case TurbineState::RESTRICTED:
        // 保护限制 → 只能恢复到正常（同样需要确认时间）
        if (S > recover_thr) {
            healthy_duration_sec_ += dt;
            if (healthy_duration_sec_ >= confirm_time) {
                healthy_duration_sec_ = 0.0;
                stateChanged = true;
                return TurbineState::NORMAL;
            }
        }
        else {
            healthy_duration_sec_ = 0.0;
        }
        break;

    default:
        break;
    }

    // 无状态变化
    return current;
}