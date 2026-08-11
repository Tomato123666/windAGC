#include "PredictionFusion.h"
#include "Utils.h"
#include <cmath>

PredictionFusion::PredictionFusion(PredictionFunc pred_func, double total_rated_power, double alpha_ff, double max_ff_pu)
    : pred_func_(pred_func), total_rated_power_(total_rated_power),
    alpha_ff_(alpha_ff), max_ff_pu_(max_ff_pu), confidence_(0.85),
    // 提高LPF alpha：原0.3 → 0.15，更好的去噪效果（PPT要求5min滤波）
    lpf_(0.15) {}

double PredictionFusion::computeFeedforward(double schedule_power, double current_time_sec) {
    // 预测未来30s后的功率（模拟超短期预测15min分辨率下的30s内插）
    double pred_power = pred_func_(current_time_sec + 30.0);

    // 计算预测偏差
    double delta = schedule_power - pred_power;

    // 一阶低通滤波去噪（模拟PPT要求的5min窗口平滑）
    double filtered_delta = lpf_.filter(delta);

    // 动态置信度：基于偏差大小自适应调整
    // 偏差越大 → 置信度越低 → 前馈量越保守
    double delta_pu = std::fabs(filtered_delta) / total_rated_power_;
    double adaptive_confidence = confidence_ * (1.0 - std::min(delta_pu * 2.0, 0.5));
    adaptive_confidence = Utils::clamp(adaptive_confidence, 0.3, 0.95);

    // 前馈量 = 滤波偏差 × 前馈系数 × 动态置信度
    double ff = alpha_ff_ * adaptive_confidence * filtered_delta;

    // 安全限幅（PPT要求 |P_ff| ≤ 15% Pn）
    double max_ff = max_ff_pu_ * total_rated_power_;
    return Utils::clamp(ff, -max_ff, max_ff);
}
