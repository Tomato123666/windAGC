// ============================================================================
// distributor.h
// ============================================================================
#ifndef DISTRIBUTOR_H
#define DISTRIBUTOR_H

#include "agc_types.h"

/**
 * @class Distributor
 * @brief 在线功率分配器
 *
 * 根据当前运行模式（NORMAL_TRACKING / DEEP_CURTAILMENT）将总目标功率
 * 分配到各台风机，写入 power_target_raw 字段，并初步更新 power_setpoint。
 * 内部处理 REGULATING 机组的权重分配、征召 BASE 机组为 BORROWED_REG，
 * 以及限幅迭代等逻辑。
 */
class Distributor {
public:
    /**
     * @brief 执行功率分配
     * @param farm        全场状态（将被修改）
     * @param target_total 已经过校核的全场目标功率 (MW)
     * @param cfg         全局配置参数
     */
    static void distribute(FarmState& farm, double target_total,
        const AgcConfig& cfg);
};

#endif // DISTRIBUTOR_H