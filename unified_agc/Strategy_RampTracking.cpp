#include "Strategy_RampTracking.h"
#include <cmath>

namespace unified {

bool StrategyRampTracking::initialize(int turbineCount, double turbineRatedMW, double totalRatedMW) {
    StrategyBase::initialize(turbineCount, turbineRatedMW, totalRatedMW);
    pid_ = common::PIDController(0.3, 0.0, 0.0, 0.1, 5.0, -5.0, 5.0);
    return true;
}

ControlResult StrategyRampTracking::step(const DispatchCommand& cmd, FarmStatus& farm, double dtSec) {
    ControlResult result;
    lastCommands_.clear();

    if (!tracking_) {
        startPower_ = farm.totalPowerMW;
        targetPower_ = cmd.targetPowerMW;
        rampRateMWmin_ = (cmd.commandType == CommandType::RAMP && cmd.rampRateMWmin > 0)
                         ? cmd.rampRateMWmin : 50.0;
        tracking_ = true; elapsedSec_ = 0.0; pid_.reset();
    }
    elapsedSec_ += dtSec;

    double desiredTarget;
    if (cmd.commandType == CommandType::STEP || rampRateMWmin_ >= 100.0) {
        desiredTarget = targetPower_;
    } else {
        double rampPerSec = rampRateMWmin_ / 60.0;
        double delta = rampPerSec * elapsedSec_;
        desiredTarget = (targetPower_ > startPower_)
            ? std::min(startPower_ + delta, targetPower_)
            : std::max(startPower_ - delta, targetPower_);
    }

    double error = desiredTarget - farm.totalPowerMW;
    double compensation = pid_.compute(error);
    double compensated = common::clamp(desiredTarget + compensation, 0.0, farm.totalAvailableMW);

    bool safe = true;
    for (auto& t : turbines_) {
        if (t.torqueKNm > maxTorque_ * 1.1 || t.bladeRootMoment > maxBladeMoment_ * 1.1) {
            safe = false; result.message = "PROTECTION: limit exceeded"; break;
        }
    }
    if (!safe) { result.success = false; return result; }

    int nRunning = 0;
    for (auto& t : turbines_)
        if (t.state == TurbineRunState::NORMAL) nRunning++;
    if (nRunning == 0) { result.success = false; return result; }

    double perTurbine = compensated / nRunning;
    for (auto& t : turbines_) {
        double pow = (t.state == TurbineRunState::NORMAL)
            ? common::clamp(perTurbine, t.powerAvailableMin, t.powerAvailableMax) : 0.0;
        TurbineCommand tc; tc.turbineId = t.turbineId; tc.powerSetMW = pow;
        lastCommands_.push_back(tc);
        t.powerMW = common::rampLimit(t.powerMW, pow, 50.0, dtSec);
        t.torqueKNm = (pow / t.powerAvailableMax) * 1000.0;
    }

    if (std::abs(farm.totalPowerMW - targetPower_) < deadbandMW_) {
        tracking_ = false; result.message = "TRACKING_COMPLETE";
    }

    farm.controlMode = FarmControlMode::RAMP_TRACKING;
    farm.targetPowerMW = targetPower_;
    updateFarm(farm);
    result.success = true; result.commands = lastCommands_;
    return result;
}

void StrategyRampTracking::updateFarm(FarmStatus& farm) {
    double total = 0;
    for (auto& t : turbines_) total += t.powerMW;
    farm.totalPowerMW = total;
    farm.totalAvailableMW = turbineCount_ * turbineRatedMW_;
}

} // namespace unified
