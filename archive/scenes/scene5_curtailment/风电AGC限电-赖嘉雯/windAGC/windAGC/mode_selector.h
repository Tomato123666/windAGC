// ============================================================================
// mode_selector.h
// ============================================================================
#ifndef MODE_SELECTOR_H
#define MODE_SELECTOR_H

#include "agc_types.h"

/**
 * @class ModeSelector
 * @brief 全场运行模式选择器
 *
 * 根据当前限电比例与预测持续时长，在 NORMAL_TRACKING 与 DEEP_CURTAILMENT
 * 两种模式间切换，并内置滞环逻辑防止频繁抖动。
 */
class ModeSelector {
public:
    /**
     * @brief 构造函数，绑定全局配置
     * @param config 全局AGC参数，使用 R_thresh 与 T_thresh
     */
    explicit ModeSelector(const AgcConfig& config);

    /**
     * @brief 更新运行模式
     * @param R_curtail       当前限电比例 (0~1)
     * @param T_curtail_hours 预测限电持续时长 (小时)
     * @param currentMode     当前运行模式
     * @return 推荐的新运行模式
     */
    OperationMode update(double R_curtail, double T_curtail_hours,
        OperationMode currentMode);

private:
    const AgcConfig& cfg_;
};

#endif // MODE_SELECTOR_H