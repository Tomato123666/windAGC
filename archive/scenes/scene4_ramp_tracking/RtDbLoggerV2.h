/**
 * @file RtDbLoggerV2.h
 * @brief 场景4调度指令阶跃斜坡变化 —— 统合架构 RT_DB 日志记录器 (V2)
 *
 * 继承自 common::RtDbLoggerBase，注册场景4全部数据点。
 * 调度指令通过 RT_DB 命令队列（ControlCommand）接收，支持 STEP/RAMP 双向映射。
 *
 * 注册数据点:
 *   WIND_AGC  (8): TotalPower, SchedulePower, Setpoint, ErrorPU,
 *                   FeedforwardMW, FeedbackMW, Mode, CycleCount
 *   TURBINE   (6): 000~001 .Power, .Command, .WindSpeed
 *   RAMP      (6): CurrentTarget, DesiredTarget, CompensatedTarget,
 *                   IsActive, RampRateMWmin, ProtectionSafe
 */
#pragma once

#include "../common/RtDbLoggerBase.h"
#include "../common/CommonTypes.h"
#include <cstdio>
#include <cstring>
#include <cstdint>

class RtDbLoggerV2 : public common::RtDbLoggerBase {
public:
    RtDbLoggerV2()
        : common::RtDbLoggerBase("Scene4-DispatchRamp")
    {
        for (int i = 0; i < MAX_TURB; i++) {
            idxPower_[i]    = (size_t)-1;
            idxCommand_[i]  = (size_t)-1;
            idxWindSpeed_[i] = (size_t)-1;
        }
    }

    ~RtDbLoggerV2() override = default;

    // =========================================================
    // 便捷日志接口
    // =========================================================

    /**
     * 每个控制步调用: 写入全场跟踪状态
     * @param step          控制步序号
     * @param totalPowerMW  全场实际总功率 (MW)
     * @param targetPowerMW 当前爬坡目标 (MW)
     * @param errorMW       跟踪误差 (MW)
     * @param controlMode   控制模式 (对应 FarmControlMode 枚举值)
     * @param compensation  前馈补偿量 (MW) — 写入 FeedforwardMW
     */
    void logStep(int step, float totalPowerMW, float targetPowerMW,
                 float errorMW, int controlMode, float compensation)
    {
        writePoint(idxTotalPower_,    totalPowerMW);
        writePoint(idxSetpoint_,      targetPowerMW);
        writePoint(idxErrorPU_,       errorMW);
        writePoint(idxMode_,          (double)controlMode);
        writePoint(idxFeedforwardMW_, compensation);
        writePoint(idxCycleCount_,    (double)step);
    }

    /**
     * 写入单台风机运行状态
     * @param turbineId    风机编号 (0 或 1)
     * @param powerMW      当前输出功率 (MW)
     * @param commandMW    下发给风机的功率指令 (MW)
     * @param availableMax 最大可用功率 / 风速 (MW 或 m/s)
     * @param state        风机状态 (对应 TurbineRunState 枚举值)
     */
    void logTurbineState(int turbineId, float powerMW, float commandMW,
                         float availableMax, int state)
    {
        if (turbineId < 0 || turbineId >= MAX_TURB) return;
        int i = turbineId;
        writePoint(idxPower_[i],     powerMW);
        writePoint(idxCommand_[i],   commandMW);
        writePoint(idxWindSpeed_[i], availableMax);
        // state is tracked via TurbineRunState enum; written to WindSpeed
        // quality field or could be extended with a dedicated point
        (void)state;
    }

    // =========================================================
    // 指令队列集成
    // =========================================================

    /**
     * 非阻塞轮询外部调度指令（从共享内存 ControlCommand 队列）
     * @param[out] targetMW  调度目标功率 (MW)
     * @param[out] cmdType   指令类型 (1=STEP, 2=RAMP, 对应 CommandType)
     * @param[out] rampRate  爬坡速率 (MW/min), STEP 时为 0
     * @param[out] priority  优先级
     * @return true=有新指令, false=队列空
     */
    bool pollDispatchCommand(float& targetMW, int& cmdType,
                             float& rampRate, uint8_t& priority)
    {
        ControlCommand cmd;
        if (!popCommand(&cmd)) return false;

        targetMW = (float)cmd.value;
        cmdType  = cmd.command_type;
        // type=1 → STEP, type=2 → RAMP, type=0 → CANCEL
        rampRate = (cmdType == 2) ? 5.0f : 0.0f;   // 默认爬坡率 5 MW/min
        priority = (uint8_t)cmd.priority;

        std::printf("[RtDbLoggerV2] <<< 收到外部调度指令: device=%s, "
                    "target=%.1f MW, type=%s, priority=%d\n",
                    cmd.device_id, cmd.value,
                    (cmdType == 1) ? "STEP" : (cmdType == 2) ? "RAMP" : "OTHER",
                    cmd.priority);
        return true;
    }

    /**
     * 推送当前指令状态到共享内存（供外部监控）
     * @param cmdId    指令ID
     * @param targetMW 目标功率
     * @param cmdType  指令类型
     */
    void logCommandAccepted(uint32_t cmdId, float targetMW, int cmdType)
    {
        std::printf("[RtDbLoggerV2] 指令已接受: id=%u, target=%.1f MW, "
                    "type=%s\n",
                    cmdId, targetMW,
                    (cmdType == 0) ? "STEP" : "RAMP");
    }

    /**
     * 跟踪完成时调用 — 写入最终误差到共享内存
     * @param finalPower 最终全场功率 (MW)
     * @param finalError 最终跟踪误差 (MW)
     */
    void logTrackingComplete(float finalPower, float finalError)
    {
        writePoint(idxErrorPU_, finalError);
        std::printf("[RtDbLoggerV2] 跟踪完成: final=%.2f MW, error=%.2f MW\n",
                    finalPower, finalError);
    }

    // =========================================================
    // 扩展日志接口 —— 写入 V2 新增数据点
    // =========================================================

    /**
     * 单独写入调度计划功率 (WIND_AGC.SchedulePower)
     */
    void logSchedulePower(float scheduleMW)
    {
        writePoint(idxSchedulePower_, scheduleMW);
    }

    /**
     * 单独写入反馈补偿量 (WIND_AGC.FeedbackMW)
     */
    void logFeedbackMW(float feedbackMW)
    {
        writePoint(idxFeedbackMW_, feedbackMW);
    }

    /**
     * 写入爬坡全过程状态 (RAMP.* 数据点)
     * @param currentTarget      当前爬坡中间目标 (MW)
     * @param desiredTarget      调度期望最终目标 (MW)
     * @param compensatedTarget  经前馈/反馈补偿后的目标 (MW)
     * @param isActive           爬坡是否活跃
     * @param rampRate           实际爬坡速率 (MW/min)
     * @param protectionSafe     保护管理器判定安全 (true=安全)
     */
    void logRampState(float currentTarget, float desiredTarget,
                      float compensatedTarget, bool isActive,
                      float rampRate, bool protectionSafe)
    {
        writePoint(idxRampCurrentTarget_,     currentTarget);
        writePoint(idxRampDesiredTarget_,     desiredTarget);
        writePoint(idxRampCompensatedTarget_, compensatedTarget);
        writePoint(idxRampIsActive_,          isActive ? 1.0 : 0.0);
        writePoint(idxRampRateMWmin_,         rampRate);
        writePoint(idxRampProtectionSafe_,    protectionSafe ? 1.0 : 0.0);
    }

protected:
    // =========================================================
    // buildPointIndexMap — 注册场景4全部 20 个数据点
    // =========================================================
    void buildPointIndexMap() override
    {
        // --- WIND_AGC 数据点 (8个) ---
        registerPoint("WIND_AGC.TotalPower",    &idxTotalPower_);
        registerPoint("WIND_AGC.SchedulePower", &idxSchedulePower_);
        registerPoint("WIND_AGC.Setpoint",      &idxSetpoint_);
        registerPoint("WIND_AGC.ErrorPU",       &idxErrorPU_);
        registerPoint("WIND_AGC.FeedforwardMW", &idxFeedforwardMW_);
        registerPoint("WIND_AGC.FeedbackMW",    &idxFeedbackMW_);
        registerPoint("WIND_AGC.Mode",          &idxMode_);
        registerPoint("WIND_AGC.CycleCount",    &idxCycleCount_);

        // --- TURBINE 数据点 (2台 × 3 = 6个) ---
        char buf[64];
        for (int t = 0; t < MAX_TURB; t++) {
            snprintf(buf, sizeof(buf), "TURBINE_%03d.Power", t);
            registerPoint(buf, &idxPower_[t]);
            snprintf(buf, sizeof(buf), "TURBINE_%03d.Command", t);
            registerPoint(buf, &idxCommand_[t]);
            snprintf(buf, sizeof(buf), "TURBINE_%03d.WindSpeed", t);
            registerPoint(buf, &idxWindSpeed_[t]);
        }

        // --- RAMP 数据点 (6个) ---
        registerPoint("RAMP.CurrentTarget",     &idxRampCurrentTarget_);
        registerPoint("RAMP.DesiredTarget",     &idxRampDesiredTarget_);
        registerPoint("RAMP.CompensatedTarget", &idxRampCompensatedTarget_);
        registerPoint("RAMP.IsActive",          &idxRampIsActive_);
        registerPoint("RAMP.RampRateMWmin",     &idxRampRateMWmin_);
        registerPoint("RAMP.ProtectionSafe",    &idxRampProtectionSafe_);
    }

private:
    static const int MAX_TURB = 2;

    // ---- WIND_AGC 数据点索引 (8) ----
    size_t idxTotalPower_    = (size_t)-1;
    size_t idxSchedulePower_ = (size_t)-1;
    size_t idxSetpoint_      = (size_t)-1;
    size_t idxErrorPU_       = (size_t)-1;
    size_t idxFeedforwardMW_ = (size_t)-1;
    size_t idxFeedbackMW_    = (size_t)-1;
    size_t idxMode_          = (size_t)-1;
    size_t idxCycleCount_    = (size_t)-1;

    // ---- TURBINE 数据点索引 (2×3) ----
    size_t idxPower_[2];
    size_t idxCommand_[2];
    size_t idxWindSpeed_[2];

    // ---- RAMP 数据点索引 (6) ----
    size_t idxRampCurrentTarget_     = (size_t)-1;
    size_t idxRampDesiredTarget_     = (size_t)-1;
    size_t idxRampCompensatedTarget_ = (size_t)-1;
    size_t idxRampIsActive_          = (size_t)-1;
    size_t idxRampRateMWmin_         = (size_t)-1;
    size_t idxRampProtectionSafe_    = (size_t)-1;
};
