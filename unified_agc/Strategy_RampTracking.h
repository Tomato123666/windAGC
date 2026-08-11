#pragma once
#include "StrategyBase.h"

namespace unified {

class StrategyRampTracking : public StrategyBase {
public:
    const char* name() const override { return "Ramp Tracking (Step/Ramp)"; }
    int sceneId() const override { return 4; }
    bool initialize(int turbineCount, double turbineRatedMW, double totalRatedMW) override;
    ControlResult step(const DispatchCommand& cmd, FarmStatus& farm, double dtSec) override;
    bool isTracking() const { return tracking_; }
    double getTarget() const { return targetPower_; }

private:
    void updateFarm(FarmStatus& farm);

    common::PIDController pid_{0.3, 0.0, 0.0, 0.1, 5.0, -5.0, 5.0};
    bool tracking_ = false;
    double startPower_ = 0.0, targetPower_ = 0.0;
    double elapsedSec_ = 0.0, rampRateMWmin_ = 5.0;
    double deadbandMW_ = 0.5;
    double maxTorque_ = 1500.0, maxBladeMoment_ = 5000.0;
};

} // namespace unified
