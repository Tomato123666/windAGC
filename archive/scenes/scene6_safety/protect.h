#pragma once
#include "types.h"
#include <cstdio>
#include <cmath>

// ====== 保护模块 (李浚晞 增强) ======

inline bool protect_can_restart(const TurbineUnit* fan, float simTimeSec) {
    if (fan->status != FanState::FAN_STOPPED_LOCK) return true;
    float stopped = simTimeSec - fan->stop_start_time;
    if (stopped >= MIN_STOP_SEC) return true;
    std::printf("[保护] 风机%d 停机不足(%.0f/%.0fs) 禁止重启\n",
                fan->id, stopped, MIN_STOP_SEC);
    return false;
}

inline int protect_avc_check(float voltage) {
    if (voltage > VOLTAGE_UPPER) { std::printf("[AVC] 电压越上限 %.2f pu\n", voltage); return 1; }
    if (voltage < VOLTAGE_LOWER) { std::printf("[AVC] 电压越下限 %.2f pu\n", voltage); return -1; }
    return 0;
}

// 极端天气功率削减策略
inline float extreme_power_cut(ExtremeSubType sub, float currentPower,
                                float ratedPower, float turbulence) {
    switch (sub) {
    case ExtremeSubType::CUT_OUT:
        return currentPower * 0.70f;  // 紧急降载30%
    case ExtremeSubType::HIGH_TURB:
        return currentPower * (1.0f - 0.15f * (turbulence / TURB_THRESHOLD));
    case ExtremeSubType::STORM_RIDE: {
        float minPwr = ratedPower * 0.10f;
        float reduced = currentPower * 0.75f;  // 柔性降载25%
        return (reduced < minPwr) ? minPwr : reduced;
    }
    default: return currentPower;
    }
}
