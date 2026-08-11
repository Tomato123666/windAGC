#include "UnifiedAGC.h"
#include <cmath>

namespace unified {

UnifiedAGC::UnifiedAGC(const Config& cfg) : config_(cfg) {
    strategies_[0] = std::make_unique<StrategyNormal>();
    strategies_[1] = std::make_unique<StrategyDisturbance>();
    strategies_[2] = std::make_unique<StrategyFrequencyReg>();
    strategies_[3] = std::make_unique<StrategyRampTracking>();
    strategies_[4] = std::make_unique<StrategyCurtailment>();
    strategies_[5] = std::make_unique<StrategySafety>();
}

bool UnifiedAGC::initialize() {
    for (auto& s : strategies_)
        s->initialize(config_.turbineCount, config_.turbineRatedMW, config_.totalRatedMW);

    farm_.turbines.resize(config_.turbineCount);
    for (int i = 0; i < config_.turbineCount; i++) {
        auto& t = farm_.turbines[i];
        t.turbineId = i;
        t.state = TurbineRunState::NORMAL;
        t.powerAvailableMax = config_.turbineRatedMW;
        t.powerAvailableMin = config_.turbineRatedMW * 0.05;
        t.powerMW = config_.turbineRatedMW * 0.5;
        t.powerSetMW = t.powerMW;
        t.windSpeedMs = 12.0;
        t.efficiency = 0.85;
        t.safetyIndex = 1.0;
    }
    farm_.totalPowerMW = config_.totalRatedMW * 0.5;
    farm_.totalAvailableMW = config_.totalRatedMW;
    farm_.frequencyHz = 50.0;
    farm_.voltagePU = 1.0;
    farm_.commHealthy = true;
    farm_.safetyMode = SafetySubMode::NORMAL;
    farm_.controlMode = FarmControlMode::STEADY_STATE;
    farm_.avgWindSpeedMs = 12.0;
    farm_.avgTurbulence = 0.05;

    printf("[UnifiedAGC] %d x %.1f MW = %.0f MW | 6 strategies ready\n",
           config_.turbineCount, config_.turbineRatedMW, config_.totalRatedMW);
    return true;
}

void UnifiedAGC::step(const DispatchCommand& cmd, double dtSec) {
    auto cond = detector_.detect(farm_, cmd);
    int newScene = cond.sceneId;

    if (newScene != activeScene_) {
        if (switchLogCount_++ < 15)
            printf("[AGC] %d->%d: %s\n", activeScene_, newScene, cond.reason);
        activeScene_ = newScene;
        farm_.safetyMode = SafetySubMode::NORMAL;
        farm_.frozenPowerMW = 0;
    }

    // Simulate frequency recovery in FR mode
    if (activeScene_ == 3 && std::abs(farm_.frequencyHz - 50.0) > 0.005) {
        double err = farm_.frequencyHz - 50.0;
        farm_.frequencyHz -= err * 0.02;
        if (std::abs(farm_.frequencyHz - 50.0) < 0.005)
            farm_.frequencyHz = 50.0;
    }

    auto& st = strategies_[activeScene_ - 1];
    ControlResult result = st->step(cmd, farm_, dtSec);

    if (result.success && !result.commands.empty()) {
        for (size_t i = 0; i < result.commands.size() && i < farm_.turbines.size(); i++)
            farm_.turbines[i].powerSetMW = result.commands[i].powerSetMW;
        for (auto& t : farm_.turbines)
            t.powerMW += (t.powerSetMW - t.powerMW) * 0.3;
    }

    double total = 0;
    for (auto& t : farm_.turbines) total += t.powerMW;
    farm_.totalPowerMW = total;
    farm_.schedulePowerMW = cmd.targetPowerMW;
    farm_.MAE = std::abs(farm_.totalPowerMW - cmd.targetPowerMW) / farm_.totalAvailableMW * 100.0;
    farm_.reserveUpMW = farm_.totalAvailableMW - farm_.totalPowerMW;
    farm_.reserveDownMW = farm_.totalPowerMW - farm_.totalMinPowerMW;

    if (logger_ && logger_->isConnected())
        logger_->logFarmState(farm_, activeScene_);

    cycle_++;
}

void UnifiedAGC::injectFreqDisturbance(double hz) {
    farm_.frequencyHz = hz;
    printf("[AGC] EVENT: Freq disturbance %.2f Hz\n", hz);
}
void UnifiedAGC::setCommHealthy(bool ok) {
    farm_.commHealthy = ok;
    printf("[AGC] EVENT: Comm %s\n", ok ? "RESTORED" : "LOST");
}
void UnifiedAGC::setExtremeWeather(ExtremeSubType t, double ws, double turb) {
    farm_.extremeType = t; farm_.avgWindSpeedMs = ws; farm_.avgTurbulence = turb;
}
void UnifiedAGC::clearExtremeWeather() {
    farm_.extremeType = ExtremeSubType::NONE;
    farm_.avgWindSpeedMs = 12.0; farm_.avgTurbulence = 0.05;
}
void UnifiedAGC::setWindSpeed(double ws) {
    farm_.avgWindSpeedMs = ws;
    for (auto& t : farm_.turbines) {
        t.windSpeedMs = ws + t.turbineId * 0.1;
        t.powerAvailableMax = config_.turbineRatedMW * std::min(1.0, ws / 12.0);
        t.powerAvailableMax = common::clamp(t.powerAvailableMax,
            config_.turbineRatedMW * 0.1, config_.turbineRatedMW);
    }
    farm_.totalAvailableMW = config_.totalRatedMW * std::min(1.0, ws / 12.0);
}
void UnifiedAGC::setCurtailHours(double h) {
    static_cast<StrategyCurtailment&>(*strategies_[4]).setCurtailHours(h);
}
const char* UnifiedAGC::strategyName() const {
    return strategies_[activeScene_-1]->name();
}

} // namespace unified
