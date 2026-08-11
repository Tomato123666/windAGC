#pragma once
#include "types.h"
#include <cstdio>

// ====== 24h 调度仿真 (李浚晞 增强) ======

// 24小时风速因子: 模拟凌晨低谷→午间高峰→夜间衰减
static const float hour_wind_factor[24] = {
    0.40f, 0.35f, 0.30f, 0.30f, 0.35f, 0.45f, 0.60f, 0.75f, 0.90f, 1.00f,
    1.10f, 1.20f, 1.20f, 1.15f, 1.10f, 1.00f, 0.90f, 0.80f, 0.70f, 0.60f,
    0.55f, 0.50f, 0.45f, 0.40f
};

// 按小时获取限电模式
inline CurtailMode get_mode_by_hour(int hour) {
    if (hour <= 6)  return CurtailMode::MODE_B_PART_RUN;
    if (hour <= 17) return CurtailMode::MODE_A_UNIFORM;
    return CurtailMode::MODE_C_ROTOR_ENERGY;
}

// 按小时获取限电比例
inline float get_curtail_ratio(int hour) {
    if (hour <= 6)  return 0.30f;
    if (hour <= 12) return 0.15f;
    if (hour <= 17) return 0.10f;
    return 0.20f;
}

// 按小时获取场站电压 (模拟日负荷波动)
inline float get_station_voltage(int hour) {
    return 0.98f + (24 - hour) * 0.001667f;
}

// 获取计划功率曲线 (40MW × 风因子 × 限电补偿)
inline float get_schedule_power(int hour) {
    return RATED_POWER_MW * hour_wind_factor[hour];
}

// 是否为备用响应时段
inline bool is_reserve_hour(int hour) {
    return (hour == 9 || hour == 19);
}

// 模拟通信状态 (基于时段)
inline bool simulate_comm_status(int hour, int minute) {
    // 凌晨3:00-3:30 和 下午15:00-15:30 模拟通信中断
    if (hour == 3 && minute < 30) return false;
    if (hour == 15 && minute < 30) return false;
    return true;
}

// 模拟极端天气 (基于时段)
inline bool simulate_extreme_wind(int hour, int minute, float& windSpeed, float& turbulence) {
    // 中午11:00-12:00 模拟风暴穿越
    if (hour == 11) {
        windSpeed = 22.0f; turbulence = 0.20f; return true;
    }
    // 下午14:00-14:30 模拟高湍流
    if (hour == 14 && minute < 30) {
        windSpeed = 18.0f; turbulence = 0.30f; return true;
    }
    // 下午17:00-17:30 模拟切出风速
    if (hour == 17 && minute < 30) {
        windSpeed = 26.0f; turbulence = 0.20f; return true;
    }
    return false;
}
