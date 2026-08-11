// ============================================================================
// state_machine.h
// ============================================================================
#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include "agc_types.h"

/**
 * @class TurbineStateMachine
 * @brief 单台风机保护状态机
 *
 * 根据安全指数 S 驱动 NORMAL、SHADOW_RESTRICTED、RESTRICTED 三种状态之间的转移。
 * HOT_STANDBY、FAULT、BORROWED_REG 不受 S 影响，由外部直接设置。
 * 内部维护健康状态持续时间，用于带滞环的恢复确认。
 */
class TurbineStateMachine {
public:
    /**
     * @brief 构造函数，绑定全局配置参数
     * @param config 全局AGC配置，仅读取 S_warn_thr, S_force_thr
     */
    explicit TurbineStateMachine(const AgcConfig& config);

    /**
     * @brief 更新状态机
     * @param current 当前风机状态
     * @param S       最新的安全指数 (0~1)
     * @param dt      距离上次调用的时间步长 (秒)
     * @param stateChanged [输出] 本次调用是否发生了状态转移
     * @return 转移后的新状态
     */
    TurbineState update(TurbineState current, double S, double dt,
        bool& stateChanged);

private:
    const AgcConfig& cfg_;           ///< 全局配置引用
    double healthy_duration_sec_;    ///< 当前健康累计时长 (秒)
};

#endif // STATE_MACHINE_H