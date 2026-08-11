/**
 * @file RtDbLoggerV2.h
 * @brief 场景6 V2 RT_DB日志器 —— 继承common::RtDbLoggerBase，统一架构
 *
 * 管理场景6所需的所有共享内存数据点：
 *   WIND_AGC.*  GRID.*  SAFETY.*  COMM.*  EXTREME.*  AVC.*  SCHEDULE.*
 *
 * 使用方式:
 *   RtDbLoggerV2 logger;
 *   logger.initialize();               // 连接RT_DB，注册数据点
 *   logger.logState(farmStatus);       // 每控制周期写入全场状态
 *   logger.logSafetyEvent(...);        // 安全模式切换事件
 *   logger.logCommEvent(...);          // 通信状态变化事件
 *   logger.logExtremeEvent(...);       // 极端天气触发事件
 *   logger.logAVCState(...);           // AVC电压状态
 *   logger.logCurtailState(...);       // 限电调度状态
 */
#pragma once

#include "../common/RtDbLoggerBase.h"
#include "../common/CommonTypes.h"

#include <cstdio>
#include <cmath>

// ============================================================================
// RtDbLoggerV2
// ============================================================================

class RtDbLoggerV2 : public common::RtDbLoggerBase {
public:
    // ------------------------------------------------------------------
    // 构造 / 析构
    // ------------------------------------------------------------------

    RtDbLoggerV2()
        : common::RtDbLoggerBase("Scenario6-V2")
    {}

    // ------------------------------------------------------------------
    // 便捷日志方法 (public, 供main.cpp调用)
    // ------------------------------------------------------------------

    /**
     * 写入全场运行状态（每控制周期调用一次）
     * 覆盖 WIND_AGC.* 及 GRID.Frequency 数据点
     */
    void logState(const FarmStatus& s) {
        writePoint(idxTotalPower_,         s.totalPowerMW);
        writePoint(idxSchedulePower_,      s.schedulePowerMW);
        writePoint(idxFeedforwardMW_,      s.totalAvailableMW);
        writePoint(idxWindSpeed_,          s.avgWindSpeedMs);
        writePoint(idxMode_,               static_cast<double>(static_cast<uint8_t>(s.controlMode)));
        writePoint(idxFluctuationWarning_, s.avgTurbulence > ProtectConstants::TURB_THRESHOLD ? 1.0 : 0.0);
        writePoint(idxErrorPU_,            s.MAE / std::max(s.totalPowerMW, 1.0));
        writePoint(idxSetpoint_,           s.frozenPowerMW);
        writePoint(idxQualifiedRate_,      s.qualifiedRate);

        writePoint(idxFrequency_,          s.frequencyHz);

        // 附带写入安全/通信/限电的核心标识位（其他细节由专用方法写入）
        writePoint(idxCurrentMode_,        static_cast<double>(static_cast<uint8_t>(s.safetyMode)));
        writePoint(idxFrozenPowerMW_,      s.frozenPowerMW);
        writePoint(idxIsHealthy_,          s.commHealthy ? 1.0 : 0.0);
        writePoint(idxCurtailRatio_,       s.curtailRatio);
        writePoint(idxCurtailMode_,        static_cast<double>(static_cast<uint8_t>(s.curtailMode)));
    }

    /**
     * 安全模式切换事件
     * @param fromMode  切换前的安全子模式
     * @param toMode    切换后的安全子模式
     * @param reason    触发原因描述
     */
    void logSafetyEvent(SafetySubMode fromMode, SafetySubMode toMode, const char* reason) {
        writePoint(idxCurrentMode_, static_cast<double>(static_cast<uint8_t>(toMode)));

        std::printf("[%s] 安全模式切换: %s → %s  原因: %s\n",
                    sceneName_,
                    safetyModeStr(fromMode),
                    safetyModeStr(toMode),
                    reason ? reason : "无");
    }

    /**
     * 通信状态变化事件
     * @param healthy  当前通信是否正常
     * @param reason   状态变化原因描述
     */
    void logCommEvent(bool healthy, const char* reason) {
        writePoint(idxIsHealthy_, healthy ? 1.0 : 0.0);

        std::printf("[%s] 通信事件: %s  原因: %s\n",
                    sceneName_,
                    healthy ? "恢复正常" : "发生中断",
                    reason ? reason : "无");
    }

    /**
     * 极端天气触发事件
     * @param subType     极端天气子类型
     * @param windSpeed   当前平均风速 (m/s)
     * @param turbulence  当前湍流强度
     */
    void logExtremeEvent(ExtremeSubType subType, double windSpeed, double turbulence) {
        writePoint(idxExtremeSubType_,       static_cast<double>(static_cast<uint8_t>(subType)));
        writePoint(idxExtremeAvgWindSpeed_,  windSpeed);
        writePoint(idxExtremeAvgTurbulence_, turbulence);
        writePoint(idxExtremeTriggered_,     subType != ExtremeSubType::NONE ? 1.0 : 0.0);

        std::printf("[%s] 极端天气: %s  风速=%.1f m/s  湍流=%.3f\n",
                    sceneName_,
                    extremeTypeStr(subType),
                    windSpeed,
                    turbulence);
    }

    /**
     * AVC 电压状态记录
     * @param voltage       场站实际电压 (pu)
     * @param targetVoltage 目标电压 (pu)
     * @param deviation     电压偏差 (pu)
     */
    void logAVCState(double voltage, double targetVoltage, double deviation) {
        writePoint(idxStationVoltage_, voltage);
        writePoint(idxTargetVoltage_,  targetVoltage);
        writePoint(idxDeviation_,      deviation);

        if (std::fabs(deviation) > 0.02) {
            std::printf("[%s] AVC电压越限: 实际=%.3f pu  目标=%.3f pu  偏差=%.3f pu\n",
                        sceneName_, voltage, targetVoltage, deviation);
        }
    }

    /**
     * 限电调度状态记录
     * @param mode        限电模式
     * @param ratio       限电比例 (0.0 ~ 1.0)
     * @param totalPower  全场实际功率 (MW)
     */
    void logCurtailState(CurtailMode mode, double ratio, double totalPower) {
        writePoint(idxCurtailMode_,  static_cast<double>(static_cast<uint8_t>(mode)));
        writePoint(idxCurtailRatio_, ratio);

        std::printf("[%s] 限电状态: %s  比例=%.1f%%  全场功率=%.2f MW\n",
                    sceneName_,
                    curtailModeStr(mode),
                    ratio * 100.0,
                    totalPower);
    }

protected:
    // ------------------------------------------------------------------
    // buildPointIndexMap (override)
    // ------------------------------------------------------------------

    void buildPointIndexMap() override {
        // --- WIND_AGC 域 (9个点) ---
        registerPoint("WIND_AGC.TotalPower",        &idxTotalPower_);
        registerPoint("WIND_AGC.SchedulePower",     &idxSchedulePower_);
        registerPoint("WIND_AGC.FeedforwardMW",     &idxFeedforwardMW_);
        registerPoint("WIND_AGC.WindSpeed",         &idxWindSpeed_);
        registerPoint("WIND_AGC.Mode",              &idxMode_);
        registerPoint("WIND_AGC.FluctuationWarning",&idxFluctuationWarning_);
        registerPoint("WIND_AGC.ErrorPU",           &idxErrorPU_);
        registerPoint("WIND_AGC.Setpoint",          &idxSetpoint_);
        registerPoint("WIND_AGC.QualifiedRate",     &idxQualifiedRate_);

        // --- GRID 域 (1个点) ---
        registerPoint("GRID.Frequency",             &idxFrequency_);

        // --- SAFETY 域 (5个点) ---
        registerPoint("SAFETY.CurrentMode",         &idxCurrentMode_);
        registerPoint("SAFETY.FrozenPowerMW",       &idxFrozenPowerMW_);
        registerPoint("SAFETY.RecoveryTargetMW",    &idxRecoveryTargetMW_);
        registerPoint("SAFETY.RecoveryRampRate",    &idxRecoveryRampRate_);
        registerPoint("SAFETY.RecoveryActive",      &idxRecoveryActive_);

        // --- COMM 域 (2个点) ---
        registerPoint("COMM.IsHealthy",             &idxIsHealthy_);
        registerPoint("COMM.LastHeartbeatSec",      &idxLastHeartbeatSec_);

        // --- EXTREME 域 (4个点) ---
        registerPoint("EXTREME.SubType",            &idxExtremeSubType_);
        registerPoint("EXTREME.AvgWindSpeed",       &idxExtremeAvgWindSpeed_);
        registerPoint("EXTREME.AvgTurbulence",      &idxExtremeAvgTurbulence_);
        registerPoint("EXTREME.Triggered",          &idxExtremeTriggered_);

        // --- AVC 域 (3个点) ---
        registerPoint("AVC.StationVoltage",         &idxStationVoltage_);
        registerPoint("AVC.TargetVoltage",          &idxTargetVoltage_);
        registerPoint("AVC.Deviation",              &idxDeviation_);

        // --- SCHEDULE 域 (5个点) ---
        registerPoint("SCHEDULE.HourOfDay",         &idxHourOfDay_);
        registerPoint("SCHEDULE.CurtailRatio",      &idxCurtailRatio_);
        registerPoint("SCHEDULE.CurtailMode",       &idxCurtailMode_);
        registerPoint("SCHEDULE.WindFactor",        &idxWindFactor_);
        registerPoint("SCHEDULE.IsReserveHour",     &idxIsReserveHour_);
    }

private:
    // ==================================================================
    // 数据点索引
    // ==================================================================

    // WIND_AGC (9)
    size_t idxTotalPower_          = static_cast<size_t>(-1);
    size_t idxSchedulePower_       = static_cast<size_t>(-1);
    size_t idxFeedforwardMW_       = static_cast<size_t>(-1);
    size_t idxWindSpeed_           = static_cast<size_t>(-1);
    size_t idxMode_                = static_cast<size_t>(-1);
    size_t idxFluctuationWarning_  = static_cast<size_t>(-1);
    size_t idxErrorPU_             = static_cast<size_t>(-1);
    size_t idxSetpoint_            = static_cast<size_t>(-1);
    size_t idxQualifiedRate_       = static_cast<size_t>(-1);

    // GRID (1)
    size_t idxFrequency_           = static_cast<size_t>(-1);

    // SAFETY (5)
    size_t idxCurrentMode_         = static_cast<size_t>(-1);
    size_t idxFrozenPowerMW_       = static_cast<size_t>(-1);
    size_t idxRecoveryTargetMW_    = static_cast<size_t>(-1);
    size_t idxRecoveryRampRate_    = static_cast<size_t>(-1);
    size_t idxRecoveryActive_      = static_cast<size_t>(-1);

    // COMM (2)
    size_t idxIsHealthy_           = static_cast<size_t>(-1);
    size_t idxLastHeartbeatSec_    = static_cast<size_t>(-1);

    // EXTREME (4)
    size_t idxExtremeSubType_      = static_cast<size_t>(-1);
    size_t idxExtremeAvgWindSpeed_ = static_cast<size_t>(-1);
    size_t idxExtremeAvgTurbulence_= static_cast<size_t>(-1);
    size_t idxExtremeTriggered_    = static_cast<size_t>(-1);

    // AVC (3)
    size_t idxStationVoltage_      = static_cast<size_t>(-1);
    size_t idxTargetVoltage_       = static_cast<size_t>(-1);
    size_t idxDeviation_           = static_cast<size_t>(-1);

    // SCHEDULE (5)
    size_t idxHourOfDay_           = static_cast<size_t>(-1);
    size_t idxCurtailRatio_        = static_cast<size_t>(-1);
    size_t idxCurtailMode_         = static_cast<size_t>(-1);
    size_t idxWindFactor_          = static_cast<size_t>(-1);
    size_t idxIsReserveHour_       = static_cast<size_t>(-1);
};
