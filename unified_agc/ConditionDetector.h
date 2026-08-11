#pragma once
#include "../common/CommonTypes.h"
#include <cmath>

namespace unified {

struct ConditionResult {
    int sceneId = 1;
    const char* reason = "NORMAL";
};

class ConditionDetector {
public:
    struct Config {
        double cutOutWindSpeed        = 25.0;
        double highTurbThreshold      = 0.25;
        double freqDeadbandHz         = 0.10;
        double windFluctuationThreshold = 1.5;
        double curtailRatioThreshold  = 0.40;
        double rampDetectionThreshold = 5.0;   // MW, higher to avoid jitter
        int    rampStableSteps        = 3;      // consecutive steps to confirm
    };

    ConditionDetector(const Config& cfg = Config{}) : config_(cfg) {}

    ConditionResult detect(const FarmStatus& farm, const DispatchCommand& cmd) {
        ConditionResult r;

        // P6: Safety mode (highest priority)
        if (!farm.commHealthy) {
            r.sceneId = 6; r.reason = "COMM_LOSS"; return r;
        }
        if (farm.extremeType != ExtremeSubType::NONE ||
            farm.avgWindSpeedMs >= config_.cutOutWindSpeed ||
            farm.avgTurbulence >= config_.highTurbThreshold) {
            r.sceneId = 6; r.reason = "EXTREME_WIND"; return r;
        }

        // P3: Frequency regulation
        if (std::abs(farm.frequencyHz - 50.0) > config_.freqDeadbandHz) {
            r.sceneId = 3; r.reason = "FREQ_DEVIATION"; return r;
        }

        // P2: Wind fluctuation
        if (std::abs(farm.avgWindSpeedMs - lastWind_) > config_.windFluctuationThreshold) {
            r.sceneId = 2; r.reason = "WIND_FLUCTUATION";
            lastWind_ = farm.avgWindSpeedMs;
            return r;
        }
        lastWind_ = farm.avgWindSpeedMs;

        // P5: Deep curtailment — triggered by high curtail ratio
        if (farm.curtailRatio >= config_.curtailRatioThreshold) {
            r.sceneId = 5; r.reason = "DEEP_CURTAILMENT"; return r;
        }

        // P4: Ramp tracking — explicit STEP/RAMP cmd with large target change
        if (cmd.commandType == CommandType::STEP || cmd.commandType == CommandType::RAMP) {
            double delta = std::abs(cmd.targetPowerMW - farm.totalPowerMW);
            // Enter ramp mode when delta exceeds threshold
            if (delta > config_.rampDetectionThreshold) {
                rampCount_++;
                rampActive_ = true;
            }
            // Stay in ramp mode until target nearly reached (hysteresis exit)
            if (rampActive_ && delta > 1.0) {
                r.sceneId = 4; r.reason = "RAMP_TRACKING"; return r;
            }
            if (delta <= 1.0) {
                rampActive_ = false;
                rampCount_ = 0;
            }
        } else {
            rampCount_ = 0;
            rampActive_ = false;
        }

        // P1: Normal AGC (default)
        r.sceneId = 1; r.reason = "NORMAL";
        return r;
    }

    void reset() { lastWind_ = 0.0; rampCount_ = 0; rampActive_ = false; }

private:
    Config config_;
    double lastWind_ = 0.0;
    int rampCount_ = 0;
    bool rampActive_ = false;
};

} // namespace unified
