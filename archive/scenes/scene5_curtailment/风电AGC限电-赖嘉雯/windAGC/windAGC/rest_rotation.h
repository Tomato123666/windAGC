// ============================================================================
// rest_rotation.h
// ============================================================================
#ifndef REST_ROTATION_H
#define REST_ROTATION_H

#include "agc_types.h"

/**
 * @class RestRotationScheduler
 * @brief 深度限电模式下的轮休调度器
 *
 * 负责选择哪些机组进入热备用(HOT_STANDBY)以降低总出力，并在限电缓解时
 * 按公平原则启动机组恢复发电。
 */
class RestRotationScheduler {
public:
    /**
     * @brief 构造函数，绑定全局配置（当前未直接使用，保留扩展性）
     */
    explicit RestRotationScheduler(const AgcConfig& config);

    /**
     * @brief 安排机组停机进入热备用
     * @param farm              全场状态（将被修改）
     * @param stop_capacity_mw  需要减少的容量 (MW)
     */
    void scheduleStops(FarmState& farm, double stop_capacity_mw);

    /**
     * @brief 安排机组从热备用启动恢复
     * @param farm               全场状态（将被修改）
     * @param start_capacity_mw  需要恢复的容量 (MW)
     */
    void scheduleStarts(FarmState& farm, double start_capacity_mw);

private:
    const AgcConfig& cfg_;
};

#endif // REST_ROTATION_H