#include "Strategy_FrequencyReg.h"
#include <cmath>

namespace unified {

ControlResult StrategyFrequencyReg::step(const DispatchCommand& cmd, FarmStatus& farm, double dtSec) {
    (void)cmd;
    ControlResult result; result.success = true;
    lastCommands_.clear();

    double deltaF = farm.frequencyHz - 50.0;

    switch (frState_) {
        case READY:
            if (std::abs(deltaF) > 0.10) { frState_ = ACTIVE; reason_ = "FR ACTIVE"; }
            break;
        case ACTIVE:
            if (std::abs(deltaF) < 0.02) { frState_ = RECOVERY; reason_ = "FR RECOVERY"; }
            break;
        case RECOVERY:
            if (std::abs(deltaF) < 0.003) { frState_ = READY; reason_ = "FR READY"; integral_ = 0.0; }
            break;
        default: break;
    }

    double frPower = 0.0;
    if (frState_ == ACTIVE || frState_ == RECOVERY) {
        double pTerm = -30.0 * deltaF;
        if (std::abs(deltaF) > 0.005) integral_ += deltaF * 0.2;
        double iTerm = -0.8 * integral_;
        frPower = pTerm + iTerm;
        frPower = common::clamp(frPower, -totalRatedMW_ * 0.05, totalRatedMW_ * 0.05);
    }

    double perTurbine = 0.0;
    int nRunning = 0;
    for (auto& t : turbines_)
        if (t.state == TurbineRunState::NORMAL) nRunning++;
    if (nRunning > 0) perTurbine = frPower / nRunning;

    for (auto& t : turbines_) {
        double p = (t.state == TurbineRunState::NORMAL)
            ? common::clamp(t.powerMW + perTurbine, t.powerAvailableMin, t.powerAvailableMax)
            : t.powerMW;
        TurbineCommand tc;
        tc.turbineId = t.turbineId; tc.powerSetMW = p;
        lastCommands_.push_back(tc); t.powerMW = p;
    }

    farm.frState = frState_ == READY ? FRState::READY :
                   frState_ == ACTIVE ? FRState::ACTIVE :
                   frState_ == RECOVERY ? FRState::RECOVERY : FRState::FAULT;
    farm.controlMode = FarmControlMode::FREQ_RESPONSE;
    updateFarm(farm);
    return result;
}

void StrategyFrequencyReg::updateFarm(FarmStatus& farm) {
    double total = 0;
    for (auto& t : turbines_) total += t.powerMW;
    farm.totalPowerMW = total;
    farm.totalAvailableMW = turbineCount_ * turbineRatedMW_;
}

} // namespace unified
