// ============================================================================
// mock_farm.cpp
// ============================================================================
#include "mock_farm.h"
#include <cmath>


MockFarm::MockFarm() {
    // 由于MockTurbine现在有默认构造函数，数组已默认初始化，我们可以安全赋值
    for (int i = 0; i < kNumTurbines; ++i) {
        turbines_[i] = MockTurbine(i + 1);  // 赋值操作，移动旧对象
    }
}
// 其余 setWindForAll, setTurbineS, applySetpoints, fillFarmState 保持不变

void MockFarm::setWindForAll(double time) {
    // 风速在 8~12 m/s 间正弦波动，周期 120 秒
    double base = 10.0;
    double amplitude = 2.0;
    double ws = base + amplitude * std::sin(time * 2 * 3.1415926535 / 120.0);
    for (int i = 0; i < kNumTurbines; ++i) {
        turbines_[i].setWind(ws + i * 0.1); // 各机微差
    }
}

void MockFarm::setTurbineS(int id, double s) {
    if (id >= 1 && id <= kNumTurbines)
        turbines_[id - 1].setS(s);
}

void MockFarm::applySetpoints(const std::array<double, kNumTurbines>& setpoints, double dt) {
    for (int i = 0; i < kNumTurbines; ++i) {
        turbines_[i].updateTelemetry(dt, setpoints[i]);
    }
}

void MockFarm::fillFarmState(FarmState& farm) const {
    farm.turbine_count = kNumTurbines;
    for (int i = 0; i < kNumTurbines; ++i) {
        farm.turbines[i].id = i + 1;
        farm.turbines[i].telemetry = turbines_[i].getTelemetry();
        // 注意：状态(state)和角色(role)由 AGC 控制，这里不覆盖
    }
}