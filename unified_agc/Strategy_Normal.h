#pragma once
#include "StrategyBase.h"
#include <algorithm>
#include <cmath>

namespace unified {

class StrategyNormal : public StrategyBase {
public:
    const char* name() const override { return "Normal AGC (Eco-Dispatch)"; }
    int sceneId() const override { return 1; }
    bool initialize(int turbineCount, double turbineRatedMW, double totalRatedMW) override;
    ControlResult step(const DispatchCommand& cmd, FarmStatus& farm, double dtSec) override;

private:
    double computeFeedforward(double scheduleMW);
    void   economicDispatch(double totalTarget, double totalAvail);
    void   updateFarmFromTurbines(FarmStatus& farm);

    common::PIDController pid_{1.0, 0.0, 0.0, 1.0, 10.0, -1.0, 1.0};
    common::LowPassFilter ffFilter_{0.15};
    double lastSetpoint_ = 0.0;
    double predictedPower_ = 0.0;
};

} // namespace unified
