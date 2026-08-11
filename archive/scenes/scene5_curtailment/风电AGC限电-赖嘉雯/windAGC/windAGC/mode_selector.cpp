// ============================================================================
// mode_selector.cpp
// ============================================================================
#include "mode_selector.h"

ModeSelector::ModeSelector(const AgcConfig& config)
    : cfg_(config)
{
}

OperationMode ModeSelector::update(double R_curtail, double T_curtail_hours,
    OperationMode currentMode)
{
    const double r_thresh = cfg_.R_thresh;
    const double t_thresh = cfg_.T_thresh;

    if (currentMode == OperationMode::DEEP_CURTAILMENT) {
        // 滞环退出条件：限电比例或时长明显下降
        if (R_curtail < r_thresh - 0.1 || T_curtail_hours < t_thresh - 0.5) {
            return OperationMode::NORMAL_TRACKING;
        }
        return OperationMode::DEEP_CURTAILMENT;
    }

    // 当前为 NORMAL_TRACKING 模式
    if (R_curtail > r_thresh && T_curtail_hours > t_thresh) {
        return OperationMode::DEEP_CURTAILMENT;
    }
    return OperationMode::NORMAL_TRACKING;
}