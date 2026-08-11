#pragma once
#include "StrategyBase.h"

namespace unified {

class StrategyDisturbance : public StrategyBase {
public:
    const char* name() const override { return "Wind Disturbance Suppression"; }
    int sceneId() const override { return 2; }
    bool initialize(int turbineCount, double turbineRatedMW, double totalRatedMW) override;
    ControlResult step(const DispatchCommand& cmd, FarmStatus& farm, double dtSec) override;

private:
    void updateFarm(FarmStatus& farm);

    enum { ECO, Suppress } mode_ = ECO;
    common::PIDController powerPID_{1.8, 0.06, 0.04, 0.05, 0.5, -0.5, 0.5};
    double lastWindSpeed_ = 0.0, avgWind_ = 12.0;
    int suppressCount_ = 0;
    double lastPitch_[100] = {};
};

} // namespace unified
