#include "Strategy_Safety.h"

namespace unified {

bool StrategySafety::initialize(int turbineCount, double turbineRatedMW, double totalRatedMW) {
    StrategyBase::initialize(turbineCount, turbineRatedMW, totalRatedMW);
    return true;
}

ControlResult StrategySafety::step(const DispatchCommand& cmd, FarmStatus& farm, double dtSec) {
    ControlResult result; result.success = true;
    lastCommands_.clear();

    SafetySubMode subMode = farm.safetyMode;
    if (!farm.commHealthy) {
        subMode = SafetySubMode::COMM_LOSS_FREEZE;
    } else if (farm.extremeType != ExtremeSubType::NONE ||
               farm.avgWindSpeedMs > 25.0 ||
               farm.avgTurbulence > 0.25) {
        subMode = SafetySubMode::EXTREME_WIND_AUTONOMOUS;
        if (farm.extremeType == ExtremeSubType::NONE) {
            if (farm.avgWindSpeedMs > 25.0) farm.extremeType = ExtremeSubType::CUT_OUT;
            else if (farm.avgTurbulence > 0.25) farm.extremeType = ExtremeSubType::HIGH_TURB;
            else farm.extremeType = ExtremeSubType::STORM_RIDE;
        }
    } else {
        subMode = SafetySubMode::NORMAL;
    }

    double perTurbine = 0;
    switch (subMode) {
        case SafetySubMode::COMM_LOSS_FREEZE:
            if (!frozen_) { frozenPower_ = farm.totalPowerMW; frozen_ = true; }
            perTurbine = frozenPower_ / turbineCount_;
            break;

        case SafetySubMode::EXTREME_WIND_AUTONOMOUS: {
            double factor = 1.0;
            switch (farm.extremeType) {
                case ExtremeSubType::CUT_OUT:    factor = 0.70; break;
                case ExtremeSubType::HIGH_TURB:  factor = 0.85; break;
                case ExtremeSubType::STORM_RIDE: factor = 0.75; break;
                default: break;
            }
            perTurbine = (totalRatedMW_ * factor) / turbineCount_;
            frozen_ = false;
            break;
        }

        case SafetySubMode::NORMAL:
            if (frozen_) {
                recoveryTarget_ = cmd.targetPowerMW > 0 ? cmd.targetPowerMW : farm.totalPowerMW;
                recoveryElapsed_ += dtSec;
                double rampMW = 2.0 * (recoveryElapsed_ / 60.0);
                double current = frozenPower_ + rampMW;
                if (current >= recoveryTarget_) { frozen_ = false; recoveryElapsed_ = 0; }
                perTurbine = std::min(current, recoveryTarget_) / turbineCount_;
            } else {
                perTurbine = cmd.targetPowerMW / turbineCount_;
            }
            farm.extremeType = ExtremeSubType::NONE;
            break;
    }

    for (auto& t : turbines_) {
        double pow = common::clamp(perTurbine, t.powerAvailableMin, t.powerAvailableMax);
        TurbineCommand tc; tc.turbineId = t.turbineId; tc.powerSetMW = pow;
        lastCommands_.push_back(tc); t.powerMW = pow;
    }

    farm.safetyMode = subMode;
    farm.frozenPowerMW = frozen_ ? frozenPower_ : 0;
    farm.controlMode = (subMode != SafetySubMode::NORMAL)
        ? FarmControlMode::EMERGENCY : FarmControlMode::STEADY_STATE;
    updateFarm(farm);
    result.commands = lastCommands_;
    return result;
}

void StrategySafety::updateFarm(FarmStatus& farm) {
    double total = 0;
    for (auto& t : turbines_) total += t.powerMW;
    farm.totalPowerMW = total;
    farm.totalAvailableMW = turbineCount_ * turbineRatedMW_;
}

} // namespace unified
