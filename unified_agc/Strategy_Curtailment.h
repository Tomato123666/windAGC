#pragma once
#include "StrategyBase.h"
#include <vector>

namespace unified {

class StrategyCurtailment : public StrategyBase {
public:
    const char* name() const override { return "Curtailment Reserve Optimization"; }
    int sceneId() const override { return 5; }
    bool initialize(int turbineCount, double turbineRatedMW, double totalRatedMW) override;
    ControlResult step(const DispatchCommand& cmd, FarmStatus& farm, double dtSec) override;
    void setCurtailHours(double h) { curtailHours_ = h; }

private:
    double computeSafetyIndex(const TurbineStatus& t);
    void updateFarm(FarmStatus& farm);

    enum { NormalTrack, DeepCurtail } curtailMode_ = NormalTrack;
    double curtailHours_ = 0.0;
    std::vector<double> hotStandbyHours_;
};

} // namespace unified
