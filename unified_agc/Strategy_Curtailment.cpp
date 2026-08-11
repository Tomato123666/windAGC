#include "Strategy_Curtailment.h"
#include <algorithm>

namespace unified {

bool StrategyCurtailment::initialize(int turbineCount, double turbineRatedMW, double totalRatedMW) {
    StrategyBase::initialize(turbineCount, turbineRatedMW, totalRatedMW);
    hotStandbyHours_.resize(turbineCount, 0.0);
    return true;
}

ControlResult StrategyCurtailment::step(const DispatchCommand& cmd, FarmStatus& farm, double dtSec) {
    (void)dtSec;
    ControlResult result; result.success = true;
    lastCommands_.clear();

    double totalAvail = 0.0;
    for (auto& t : turbines_) totalAvail += t.powerAvailableMax;
    farm.totalAvailableMW = totalAvail;

    double curtailRatio = (totalAvail > 0)
        ? (totalAvail - cmd.targetPowerMW) / totalAvail : 0.0;

    if (curtailRatio > 0.40 && curtailHours_ > 2.0) curtailMode_ = DeepCurtail;
    else if (curtailRatio < 0.30 || curtailHours_ < 1.5) curtailMode_ = NormalTrack;

    for (auto& t : turbines_) {
        t.safetyIndex = computeSafetyIndex(t);
        if (t.safetyIndex < 0.2 && t.state == TurbineRunState::NORMAL)
            t.state = TurbineRunState::RESTRICTED;
        else if (t.safetyIndex > 0.6 && t.state == TurbineRunState::RESTRICTED)
            t.state = TurbineRunState::NORMAL;
    }

    if (curtailMode_ == DeepCurtail) {
        double totalMinTech = 0, totalEff = 0;
        for (auto& t : turbines_) {
            if (t.state == TurbineRunState::NORMAL) {
                totalMinTech += t.powerAvailableMin;
                totalEff += t.powerAvailableMax * 0.9;
            }
        }
        double target = std::max(cmd.targetPowerMW, totalMinTech);
        double ratio = (totalEff > 0) ? std::min(target / totalEff, 1.0) : 1.0;
        for (auto& t : turbines_) {
            double pow = (t.state == TurbineRunState::NORMAL)
                ? std::max(t.powerAvailableMax * 0.9 * ratio, t.powerAvailableMin) : 0.0;
            TurbineCommand tc; tc.turbineId = t.turbineId; tc.powerSetMW = pow;
            lastCommands_.push_back(tc); t.powerMW = pow;
        }
    } else {
        for (auto& t : turbines_) {
            double pow = (t.state == TurbineRunState::NORMAL)
                ? t.powerAvailableMax * 0.95 : t.powerAvailableMin;
            TurbineCommand tc; tc.turbineId = t.turbineId; tc.powerSetMW = pow;
            lastCommands_.push_back(tc); t.powerMW = pow;
        }
    }

    farm.curtailRatio = curtailRatio;
    farm.controlMode = (curtailMode_ == DeepCurtail)
        ? FarmControlMode::DEEP_CURTAILMENT : FarmControlMode::STEADY_STATE;
    updateFarm(farm);
    return result;
}

double StrategyCurtailment::computeSafetyIndex(const TurbineStatus& t) {
    double s = 1.0;
    if (t.vibration > 50) s *= 0.5;
    if (t.gearboxTemp > 80) s *= 0.7;
    if (t.genTemp > 90) s *= 0.7;
    return common::clamp(s, 0.0, 1.0);
}

void StrategyCurtailment::updateFarm(FarmStatus& farm) {
    double total = 0;
    for (auto& t : turbines_) total += t.powerMW;
    farm.totalPowerMW = total;
    farm.totalAvailableMW = turbineCount_ * turbineRatedMW_;
}

} // namespace unified
