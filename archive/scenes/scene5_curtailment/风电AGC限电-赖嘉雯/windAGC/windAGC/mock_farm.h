// ============================================================================
// mock_farm.h
// ============================================================================
#ifndef MOCK_FARM_H
#define MOCK_FARM_H

#include <array>
#include "mock_turbine.h"
#include "agc_types.h"

/**
 * @brief 模拟风电场（10台风机）
 *
 * 维护一组 MockTurbine，提供风况更新、应用功率设定值、
 * 将遥测数据填充到 FarmState 的接口。
 */
class MockFarm {
public:
    static constexpr int kNumTurbines = 10;

    MockFarm();

    /// 根据仿真时间更新所有风机的风速（正弦波动模式）
    void setWindForAll(double time);

    /// 直接设置特定风机的安全健康度
    void setTurbineS(int id, double s);

    /// 将 AGC 下发的设定值应用到各风机，推进 dt 秒
    void applySetpoints(const std::array<double, kNumTurbines>& setpoints, double dt);

    /// 将当前所有风机的遥测写入 FarmState
    void fillFarmState(FarmState& farm) const;

private:
    std::array<MockTurbine, kNumTurbines> turbines_;
};

#endif // MOCK_FARM_H