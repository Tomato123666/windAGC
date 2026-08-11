#pragma once
#include "types.h"
#include <functional>

// ====== 安全模式管理器 (章渲祺) ======
class SafetyModeManager {
public:
    using Callback = std::function<void(SafetySubMode, SafetySubMode)>;

    SafetyModeManager() : mode_(SafetySubMode::NORMAL), frozenPower_(0.0f) {}

    void update(bool commHealthy, bool extremeWeather, float currentPower) {
        SafetySubMode next = SafetySubMode::NORMAL;
        if (!commHealthy)
            next = SafetySubMode::COMM_LOSS_FREEZE;
        else if (extremeWeather)
            next = SafetySubMode::EXTREME_WIND_AUTONOMOUS;
        else
            next = SafetySubMode::NORMAL;

        if (next != mode_) {
            auto old = mode_; mode_ = next;
            if (cb_) cb_(old, next);
            if (next == SafetySubMode::COMM_LOSS_FREEZE)
                frozenPower_ = currentPower;
        }
    }

    SafetySubMode getMode() const { return mode_; }
    float getFrozenPower() const { return frozenPower_; }
    void onModeChange(Callback cb) { cb_ = std::move(cb); }

private:
    SafetySubMode mode_;
    float frozenPower_;
    Callback cb_;
};
