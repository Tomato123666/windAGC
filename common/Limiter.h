/**
 * @file Limiter.h
 * @brief 通用限幅与斜坡限制函数
 *
 * 所有场景通用的限幅/限速工具函数。
 */
#pragma once

#include <algorithm>
#include <cmath>

namespace common {

/**
 * 通用限幅：将value约束在[lo, hi]范围内
 */
template<typename T>
inline T clamp(T value, T lo, T hi) {
    return std::max(lo, std::min(hi, value));
}

/**
 * 死区处理：|value| < deadband 时返回0
 */
inline double deadband(double value, double band) {
    return (std::abs(value) < band) ? 0.0 : value;
}

/**
 * 斜坡限制：从current向target变化，变化速率不超过maxRate
 * @param current   当前值
 * @param target    目标值
 * @param maxRate   最大变化率（单位/秒）
 * @param dt        时间步长（秒）
 * @return          受限后的值
 */
inline double rampLimit(double current, double target, double maxRate, double dt) {
    if (dt <= 0.0) return target;
    double maxDelta = maxRate * dt;
    double delta = target - current;
    if (std::abs(delta) <= maxDelta) {
        return target;
    }
    return current + std::copysign(maxDelta, delta);
}

/**
 * 带不对称限制的斜坡限制
 * @param current   当前值
 * @param target    目标值
 * @param upRate    向上最大变化率
 * @param downRate  向下最大变化率
 * @param dt        时间步长
 */
inline double rampLimitAsym(double current, double target,
                             double upRate, double downRate, double dt) {
    if (dt <= 0.0) return target;
    double delta = target - current;
    double maxDelta = (delta > 0) ? upRate * dt : downRate * dt;
    if (std::abs(delta) <= std::abs(maxDelta)) {
        return target;
    }
    return current + std::copysign(std::min(std::abs(delta), std::abs(maxDelta)), delta);
}

/**
 * 百分比限幅（将value限制在base的±percent范围内）
 */
inline double percentLimit(double value, double base, double percent) {
    double margin = std::abs(base) * percent;
    return clamp(value, base - margin, base + margin);
}

} // namespace common
