#include "Strategy_Disturbance.h"

namespace unified {

bool StrategyDisturbance::initialize(int turbineCount, double turbineRatedMW, double totalRatedMW) {
    StrategyBase::initialize(turbineCount, turbineRatedMW, totalRatedMW);
    powerPID_ = common::PIDController(1.8, 0.06, 0.04, 0.05, 0.5, -0.5, 0.5);
    return true;
}

ControlResult StrategyDisturbance::step(const DispatchCommand& cmd, FarmStatus& farm, double dtSec) {
    ControlResult result;
    result.success = true;
    lastCommands_.clear();

    double windChange = farm.avgWindSpeedMs - lastWindSpeed_;
    bool isRise = windChange > 0.5;
    bool isDrop = windChange < -0.5;

    if (isRise || isDrop) {
        if (mode_ != Suppress) {
            mode_ = Suppress;
            for (auto& t : turbines_)
                t.pitchAngleDeg = common::clamp(t.pitchAngleDeg + 0.3, 0.0, 1.0);
        }
        suppressCount_++;
    } else if (suppressCount_ > 0) {
        suppressCount_--;
    }

    if (suppressCount_ <= 0 && mode_ == Suppress) {
        mode_ = ECO;
        for (auto& t : turbines_)
            t.pitchAngleDeg = common::clamp(t.pitchAngleDeg - 0.1, 0.0, 8.0);
    }

    double gridTarget = cmd.targetPowerMW > 0 ? cmd.targetPowerMW : totalRatedMW_ * 0.5;
    double perTurbineTarget = gridTarget / turbineCount_;

    for (int i = 0; i < turbineCount_; i++) {
        auto& t = turbines_[i];
        double err = perTurbineTarget - t.powerMW;
        double pitchAdj = powerPID_.compute(err);
        t.pitchAngleDeg = common::clamp(t.pitchAngleDeg + pitchAdj * 0.25, 0.0, 8.0);
        t.pitchAngleDeg = common::rampLimit(lastPitch_[i], t.pitchAngleDeg, 0.3, dtSec);
        lastPitch_[i] = t.pitchAngleDeg;

        double maxPower = t.powerAvailableMax;
        double pitchEff = 1.0 - (t.pitchAngleDeg / 8.0) * 0.22;
        pitchEff = common::clamp(pitchEff, 0.8, 1.0);
        t.powerMW = common::rampLimit(t.powerMW, maxPower * pitchEff, 2.5, dtSec);

        TurbineCommand tc;
        tc.turbineId = t.turbineId;
        tc.powerSetMW = perTurbineTarget;
        tc.pitchAngleDeg = t.pitchAngleDeg;
        lastCommands_.push_back(tc);
    }

    farm.controlMode = (mode_ == Suppress) ? FarmControlMode::SUPPRESS : FarmControlMode::STEADY_STATE;
    updateFarm(farm);
    lastWindSpeed_ = farm.avgWindSpeedMs;
    return result;
}

void StrategyDisturbance::updateFarm(FarmStatus& farm) {
    double total = 0;
    for (auto& t : turbines_) total += t.powerMW;
    farm.totalPowerMW = total;
    farm.totalAvailableMW = turbineCount_ * turbineRatedMW_;
    farm.avgWindSpeedMs = avgWind_;
}

} // namespace unified
