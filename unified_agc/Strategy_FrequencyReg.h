#pragma once
#include "StrategyBase.h"

namespace unified {

class StrategyFrequencyReg : public StrategyBase {
public:
    const char* name() const override { return "Primary Frequency Regulation"; }
    int sceneId() const override { return 3; }
    ControlResult step(const DispatchCommand& cmd, FarmStatus& farm, double dtSec) override;
    const char* getReason() const { return reason_; }

private:
    void updateFarm(FarmStatus& farm);

    enum { READY, ACTIVE, RECOVERY, FAULT } frState_ = READY;
    double integral_ = 0.0;
    const char* reason_ = "";
};

} // namespace unified
