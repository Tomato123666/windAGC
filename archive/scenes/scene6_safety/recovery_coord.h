#pragma once
#include <cstdio>
#include <cmath>
#include <algorithm>

// ====== 恢复协调器 (章渲祺) ======
class RecoveryCoordinator {
public:
    void setRampRate(float rate) { rampRate_ = rate; }
    void start(float currentPower, float target) {
        startPower_ = currentPower; targetPower_ = target;
        recovering_ = true; elapsed_ = 0.0f;
        std::printf("[恢复] 开始: %.1f -> %.1f MW (%.1f MW/min)\n",
                    startPower_, targetPower_, rampRate_);
    }
    void update(float dtSec) { if (recovering_) elapsed_ += dtSec; }
    float getTarget(float currentPower, float schedule) {
        if (!recovering_) return schedule;
        float maxDelta = rampRate_ * (elapsed_ / 60.0f);
        float delta = targetPower_ - startPower_;
        float clamped = std::min(std::abs(delta), maxDelta);
        float expected = startPower_ + (delta > 0 ? clamped : -clamped);
        if (std::abs(currentPower - schedule) <= 0.5f && elapsed_ >= 5.0f) {
            recovering_ = false;
            std::printf("[恢复] 完成\n");
            return schedule;
        }
        return expected;
    }
    bool isRecovering() const { return recovering_; }
private:
    bool recovering_ = false;
    float rampRate_ = 2.0f;
    float startPower_ = 0, targetPower_ = 0, elapsed_ = 0;
};
