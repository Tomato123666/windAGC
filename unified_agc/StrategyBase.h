/**
 * @file StrategyBase.h
 * @brief 统一AGC策略基类 —— 所有6个场景策略继承此基类
 */
#pragma once
#include "../common/CommonTypes.h"
#include "../common/PIDController.h"
#include "../common/Limiter.h"
#include "../common/WindPowerCurve.h"
#include <string>

namespace unified {

class StrategyBase {
public:
    virtual ~StrategyBase() = default;

    /** 策略名称 */
    virtual const char* name() const = 0;

    /** 场景编号 1-6 */
    virtual int sceneId() const = 0;

    /**
     * 初始化：设置风机数量、额定功率等
     */
    virtual bool initialize(int turbineCount, double turbineRatedMW, double totalRatedMW) {
        turbineCount_ = turbineCount;
        turbineRatedMW_ = turbineRatedMW;
        totalRatedMW_ = totalRatedMW;
        turbines_.resize(turbineCount);
        for (int i = 0; i < turbineCount; i++) {
            turbines_[i].turbineId = i;
            turbines_[i].state = TurbineRunState::NORMAL;
            turbines_[i].powerAvailableMax = turbineRatedMW;
            turbines_[i].powerAvailableMin = turbineRatedMW * 0.05;
            turbines_[i].powerMW = turbineRatedMW * 0.5;
            turbines_[i].efficiency = 0.85;
        }
        return true;
    }

    /**
     * 每个控制周期调用一次
     * @param cmd      调度指令（来自电网/RT_DB）
     * @param farm     全场状态（输入/输出）
     * @param dtSec    控制周期（秒）
     * @return         控制结果
     */
    virtual ControlResult step(const DispatchCommand& cmd, FarmStatus& farm, double dtSec) = 0;

    /**
     * 获取策略输出的风机指令
     */
    const std::vector<TurbineCommand>& getTurbineCommands() const { return lastCommands_; }

protected:
    int turbineCount_ = 0;
    double turbineRatedMW_ = 3.0;
    double totalRatedMW_ = 300.0;
    std::vector<TurbineStatus> turbines_;
    std::vector<TurbineCommand> lastCommands_;
};

} // namespace unified
