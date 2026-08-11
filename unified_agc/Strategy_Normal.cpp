/**
 * @file Strategy_Normal.cpp
 * @brief 场景1: 常规AGC —— 前馈-反馈复合控制 + 经济调度（实现）
 */
#include "Strategy_Normal.h"

namespace unified {

bool StrategyNormal::initialize(int turbineCount, double turbineRatedMW, double totalRatedMW) {
    StrategyBase::initialize(turbineCount, turbineRatedMW, totalRatedMW);
    pid_ = common::PIDController(1.2, 0.15, 0.03, 1.0,
                                  0.10 * totalRatedMW,
                                  -0.20 * totalRatedMW, 0.20 * totalRatedMW);
    ffFilter_ = common::LowPassFilter(0.15);
    return true;
}

ControlResult StrategyNormal::step(const DispatchCommand& cmd, FarmStatus& farm, double dtSec) {
    ControlResult result;
    result.success = true;

    double scheduleMW = cmd.targetPowerMW;
    double actualMW   = farm.totalPowerMW;
    double totalAvail = farm.totalAvailableMW;

    double ffMW = computeFeedforward(scheduleMW);

    double errorMW = scheduleMW - actualMW;
    if (std::abs(errorMW) < 0.005 * totalRatedMW_) errorMW = 0.0;
    double fbMW = pid_.compute(errorMW);

    double setpointMW = scheduleMW + ffMW + fbMW;
    setpointMW = common::clamp(setpointMW, 0.0, totalAvail);

    double maxRamp = 0.01 * totalRatedMW_ * dtSec;
    setpointMW = common::rampLimit(lastSetpoint_, setpointMW, maxRamp / dtSec, dtSec);
    lastSetpoint_ = setpointMW;

    economicDispatch(setpointMW, totalAvail);

    farm.targetPowerMW = setpointMW;
    farm.controlMode = FarmControlMode::STEADY_STATE;
    updateFarmFromTurbines(farm);

    result.commands = lastCommands_;
    return result;
}

double StrategyNormal::computeFeedforward(double scheduleMW) {
    double delta = scheduleMW - predictedPower_;
    double filtered = ffFilter_.update(delta);
    double conf = 0.85 * (1.0 - std::min(std::abs(filtered / totalRatedMW_) * 2.0, 0.5));
    conf = common::clamp(conf, 0.3, 0.95);
    double ff = 0.75 * conf * filtered;
    return common::clamp(ff, -0.15 * totalRatedMW_, 0.15 * totalRatedMW_);
}

void StrategyNormal::economicDispatch(double totalTarget, double totalAvail) {
    lastCommands_.clear();
    int nRunning = 0;
    double totalMin = 0.0;
    std::vector<double> weights(turbineCount_, 0.0);

    for (int i = 0; i < turbineCount_; i++) {
        auto& t = turbines_[i];
        if (t.state == TurbineRunState::STOPPED || t.state == TurbineRunState::FAULT) continue;
        nRunning++;
        totalMin += t.powerAvailableMin;
        double loadRatio = t.powerMW / t.powerAvailableMax;
        double eff;
        if (loadRatio < 0.1) eff = 0.8;
        else if (loadRatio < 0.7) eff = 0.9 + (loadRatio - 0.1) * (0.1 / 0.6);
        else if (loadRatio < 0.9) eff = 1.0;
        else eff = 0.95;
        weights[i] = t.powerAvailableMax * eff;
    }
    if (nRunning == 0) return;

    double target = common::clamp(totalTarget, totalMin, totalAvail);
    double remaining = target - totalMin;
    double weightSum = 0.0;
    for (int i = 0; i < turbineCount_; i++)
        if (weights[i] > 0) weightSum += weights[i];

    for (int i = 0; i < turbineCount_; i++) {
        if (weights[i] <= 0) continue;
        auto& t = turbines_[i];
        double pow = t.powerAvailableMin;
        if (weightSum > 0) pow += remaining * (weights[i] / weightSum);
        pow = common::clamp(pow, t.powerAvailableMin, t.powerAvailableMax);
        TurbineCommand tc;
        tc.turbineId = t.turbineId;
        tc.powerSetMW = pow;
        lastCommands_.push_back(tc);
        t.powerMW = pow;
    }
}

void StrategyNormal::updateFarmFromTurbines(FarmStatus& farm) {
    double total = 0;
    for (auto& t : turbines_) total += t.powerMW;
    farm.totalPowerMW = total;
    farm.totalAvailableMW = turbineCount_ * turbineRatedMW_;
}

} // namespace unified
