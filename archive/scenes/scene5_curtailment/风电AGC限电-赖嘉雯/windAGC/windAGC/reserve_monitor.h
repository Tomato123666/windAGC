// ============================================================================
// reserve_monitor.h
// ============================================================================
#ifndef RESERVE_MONITOR_H
#define RESERVE_MONITOR_H

#include "agc_types.h"

/**
 * @class ReserveMonitor
 * @brief 备用容量监视器
 *
 * 根据全场风机的当前状态与指令，计算实时上调备用与下调备用容量。
 * 计算结果通过引用参数返回，通常由调用者写入 farm.reserve_up / reserve_down。
 */
class ReserveMonitor {
public:
    /**
     * @brief 计算备用容量
     * @param farm        全场状态（只读）
     * @param cfg         全局配置，使用 ready_factor 参数
     * @param reserve_up   [输出] 上调备用容量 (MW)
     * @param reserve_down [输出] 下调备用容量 (MW)
     */
    static void compute(const FarmState& farm, const AgcConfig& cfg,
        double& reserve_up, double& reserve_down);
};

#endif // RESERVE_MONITOR_H