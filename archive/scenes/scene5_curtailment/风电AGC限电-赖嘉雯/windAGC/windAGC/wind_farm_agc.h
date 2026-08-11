// ============================================================================
// wind_farm_agc.h
// ============================================================================
#ifndef WIND_FARM_AGC_H
#define WIND_FARM_AGC_H

#include <string>
#include <vector>
#include "agc_types.h"
#include "index_calculator.h"
#include "state_machine.h"
#include "classifier.h"
#include "mode_selector.h"
#include "rest_rotation.h"
#include "capacity_evaluator.h"
#include "command_checker.h"
#include "distributor.h"
#include "ramp_limiter.h"
#include "reserve_monitor.h"

// ---------------------------------------------------------------------------
// 若 agc_types.h 中尚未定义 AgcCommand，可在此处补充
// ---------------------------------------------------------------------------
#ifndef AGC_COMMAND_DEFINED
struct AgcCommand {
    double target_total_power;   ///< 调度下发的全场目标功率 (MW)
    bool   reserve_call;         ///< 是否调用备用
    double reserve_delta;        ///< 备用调用量 (MW)，正为上调，负为下调
};
#endif

/**
 * @class WindFarmAgc
 * @brief 风电场自动发电控制主控制器
 *
 * 将安全评估、指数计算、模式选择、机组分类、轮休调度、容量评估、
 * 指令校核、功率分配、斜坡限制、备用监视等模块串联，实现完整的
 * 风电场有功功率闭环控制。
 */
class WindFarmAgc {
public:
    /**
     * @brief 构造函数
     * @param config 全局配置参数
     */
    explicit WindFarmAgc(const AgcConfig& config);

    /**
     * @brief 设置安全阈值（所有机组共用）
     * @param params 保护阈值
     */
    void setSafetyParams(const TurbineSafetyParams& params);

    /**
     * @brief 执行一个控制步长
     * @param cmd              调度指令与备用调用
     * @param dt               控制周期 (秒)
     * @param T_curtail_hours  预计限电持续时长 (小时)，默认 1.0
     */
    void step(const AgcCommand& cmd, double dt, double T_curtail_hours = 1.0);

    /**
     * @brief 获取全场状态只读引用
     */
    const FarmState& getFarmState() const { return farm_; }

    /**
     * @brief 获取当前周期的事件日志
     */
    const std::vector<std::string>& getEventLog() const { return event_log_; }

private:
    // ---------- 配置与状态 ----------
    AgcConfig           cfg_;
    FarmState           farm_;
    TurbineSafetyParams safety_params_;

    // ---------- 功能模块 ----------
    IndexCalculator          index_calc_;
    TurbineStateMachine      state_machine_;
    Classifier               classifier_;
    ModeSelector             mode_selector_;
    RestRotationScheduler    rest_rotation_;

    // ---------- 日志 ----------
    std::vector<std::string> event_log_;

    // ---------- 内部辅助 ----------
    void logEvent(const std::string& msg);
    void applyClassifier();
};

#endif // WIND_FARM_AGC_H