#pragma once
#include "StrategyBase.h"
#include "ConditionDetector.h"
#include "UnifiedLogger.h"
#include "Strategy_Normal.h"
#include "Strategy_Disturbance.h"
#include "Strategy_FrequencyReg.h"
#include "Strategy_RampTracking.h"
#include "Strategy_Curtailment.h"
#include "Strategy_Safety.h"
#include <memory>
#include <cstdio>

namespace unified {

class UnifiedAGC {
public:
    struct Config {
        int    turbineCount   = 10;
        double turbineRatedMW = 3.0;
        double totalRatedMW   = 30.0;
    };

    explicit UnifiedAGC(const Config& cfg = Config{});
    bool initialize();
    void step(const DispatchCommand& cmd, double dtSec);

    // External events
    void injectFreqDisturbance(double hz);
    void setCommHealthy(bool ok);
    void setExtremeWeather(ExtremeSubType t, double ws, double turb);
    void clearExtremeWeather();
    void setWindSpeed(double ws);
    void setCurtailHours(double h);
    void setCurtailRatio(double r) { farm_.curtailRatio = r; }
    void setLogger(UnifiedLogger* logger) { logger_ = logger; }

    // Accessors
    const FarmStatus& farm() const { return farm_; }
    FarmStatus& farm() { return farm_; }
    int activeScene() const { return activeScene_; }
    const char* strategyName() const;
    int cycles() const { return cycle_; }

private:
    Config config_;
    FarmStatus farm_;
    ConditionDetector detector_;
    std::unique_ptr<StrategyBase> strategies_[6];
    int activeScene_ = 1, cycle_ = 0, switchLogCount_ = 0;
    UnifiedLogger* logger_ = nullptr;
};

} // namespace unified
