#pragma once
#include "types.h"
#include "protect.h"
#include <cstdio>

// ====== 风机群控制器 (李浚晞 增强) ======

inline void fan_init(TurbineUnit* fans, int count) {
    for (int i = 0; i < count; i++) {
        fans[i].id = i + 1;
        fans[i].efficiency = 0.70f + (i % 4) * 0.10f;
        fans[i].is_upwind = (i <= 3);
        fans[i].power_max_mppt = TURBINE_RATED;
        fans[i].power_now = TURBINE_RATED;
        fans[i].rotor_speed = 1500.0f;
        fans[i].status = FanState::FAN_NORMAL_MPPT;
        fans[i].stop_start_time = 0;
    }
}

inline void fan_sort(TurbineUnit* fans, int count) {
    for (int i = 0; i < count; i++)
        for (int j = i + 1; j < count; j++) {
            int p1 = (fans[i].efficiency > 0.85f && fans[i].is_upwind) ? 1 : 0;
            int p2 = (fans[j].efficiency > 0.85f && fans[j].is_upwind) ? 1 : 0;
            if (p2 > p1) { TurbineUnit t = fans[i]; fans[i] = fans[j]; fans[j] = t; }
        }
}

inline void fan_execute_mode(TurbineUnit* fans, int count, CurtailMode mode, float ratio) {
    fan_sort(fans, count);
    switch (mode) {
    case CurtailMode::MODE_A_UNIFORM:
        for (int i = 0; i < count; i++) {
            fans[i].power_now = fans[i].power_max_mppt * (1.0f - ratio);
            fans[i].status = FanState::FAN_CURTAIL_UNIFORM;
        }
        break;
    case CurtailMode::MODE_B_PART_RUN: {
        int keep = (int)(count * (1.0f - ratio));
        for (int i = 0; i < count; i++) {
            if (i < keep) {
                fans[i].power_now = fans[i].power_max_mppt;
                fans[i].status = FanState::FAN_NORMAL_MPPT;
            } else {
                fans[i].power_now = 0;
                fans[i].status = FanState::FAN_STOPPED_LOCK;
                fans[i].stop_start_time = 0; // sim time tracked externally
            }
        }
        break;
    }
    case CurtailMode::MODE_C_ROTOR_ENERGY:
        for (int i = 0; i < count; i++) {
            fans[i].rotor_speed *= 0.85f;
            fans[i].power_now = fans[i].power_max_mppt * 0.7f;
            fans[i].status = FanState::FAN_CURTAIL_SPEED;
        }
        break;
    }
}

inline void fan_restart_check(TurbineUnit* fans, int count, float simTimeSec) {
    for (int i = 0; i < count; i++) {
        if (fans[i].status == FanState::FAN_STOPPED_LOCK) {
            if (protect_can_restart(&fans[i], simTimeSec)) {
                fans[i].power_now = fans[i].power_max_mppt;
                fans[i].status = FanState::FAN_NORMAL_MPPT;
                std::printf("[风机%d] 重启成功\n", fans[i].id);
            }
        }
    }
}

inline float fan_total_power(TurbineUnit* fans, int count) {
    float s = 0; for (int i = 0; i < count; i++) s += fans[i].power_now; return s;
}
