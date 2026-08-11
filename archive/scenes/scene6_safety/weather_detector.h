#pragma once
#include "types.h"
#include <cstdio>
#include <cmath>

// ====== 极端天气检测器 (章渲祺) ======
class ExtremeWeatherDetector {
public:
    void update(float windSpeed, float turbulence) {
        avgWind_ = windSpeed; avgTurb_ = turbulence;
        bool now = (windSpeed >= CUT_OUT_SPEED) || (turbulence >= TURB_THRESHOLD);
        if (now != extreme_) {
            extreme_ = now;
            std::printf("[气象] 极端天气: %s", extreme_ ? "触发" : "解除");
            if (extreme_) {
                if (windSpeed >= CUT_OUT_SPEED) std::printf(" (切出风速 %.1f m/s)", windSpeed);
                else std::printf(" (高湍流 %.2f)", turbulence);
            }
            std::printf("\n");
        }
    }
    void forceSet(bool extreme, float wind = 0, float turb = 0) {
        avgWind_ = extreme ? (wind > 0 ? wind : 26.0f) : 12.0f;
        avgTurb_ = extreme ? (turb > 0 ? turb : 0.30f) : 0.15f;
        extreme_ = extreme;
    }
    bool isExtreme() const { return extreme_; }
    float windSpeed() const { return avgWind_; }
    float turbulence() const { return avgTurb_; }
    ExtremeSubType subType() const {
        if (!extreme_) return ExtremeSubType::NONE;
        if (avgWind_ >= CUT_OUT_SPEED) return ExtremeSubType::CUT_OUT;
        if (avgTurb_ >= TURB_THRESHOLD) return ExtremeSubType::HIGH_TURB;
        return ExtremeSubType::STORM_RIDE;
    }
private:
    bool extreme_ = false;
    float avgWind_ = 12.0f, avgTurb_ = 0.15f;
};
