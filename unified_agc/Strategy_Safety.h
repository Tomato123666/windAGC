#pragma once
#include "StrategyBase.h"

namespace unified {

class StrategySafety : public StrategyBase {
public:
    const char* name() const override { return "Safety Mode Manager"; }
    int sceneId() const override { return 6; }
    bool initialize(int turbineCount, double turbineRatedMW, double totalRatedMW) override;
    ControlResult step(const DispatchCommand& cmd, FarmStatus& farm, double dtSec) override;
    SafetySubMode getSafetyMode() const { return currentMode_; }

private:
    void updateFarm(FarmStatus& farm);

    SafetySubMode currentMode_ = SafetySubMode::NORMAL;
    bool frozen_ = false;
    double frozenPower_ = 0.0;
    double recoveryTarget_ = 0.0;
    double recoveryElapsed_ = 0.0;
};

} // namespace unified
