/**
 * @file UnifiedLogger.h
 * @brief 统一AGC系统的共享内存日志器
 */
#pragma once
#include "../common/RtDbLoggerBase.h"
#include "../common/CommonTypes.h"

namespace unified {

class UnifiedLogger : public common::RtDbLoggerBase {
public:
    UnifiedLogger() : common::RtDbLoggerBase("UnifiedAGC") {}

    /** 写入整个农场状态到共享内存 */
    void logFarmState(const FarmStatus& farm, int activeScene) {
        if (!connected_) return;

        writePoint(idxTotalPower_,    farm.totalPowerMW);
        writePoint(idxSchedulePower_, farm.schedulePowerMW);
        writePoint(idxSetpoint_,      farm.targetPowerMW);
        writePoint(idxErrorPU_,       farm.MAE);
        writePoint(idxMode_,          (double)activeScene);
        writePoint(idxWindSpeed_,     farm.avgWindSpeedMs);
        writePoint(idxFluctuation_,   farm.avgTurbulence > 0.25 ? 1.0 : 0.0);
        writePoint(idxFreq_,          farm.frequencyHz);
        writePoint(idxSafetyMode_,    (double)(int)farm.safetyMode);
        writePoint(idxCycleCount_,    (double)writeCount_++);

        // 安全/通信/限电/备用
        writePoint(idxCommHealthy_,   farm.commHealthy ? 1.0 : 0.0);
        writePoint(idxFrozenPower_,   farm.frozenPowerMW);
        writePoint(idxCurtailRatio_,  farm.curtailRatio);
        writePoint(idxExtremeType_,   (double)(int)farm.extremeType);
        writePoint(idxReserveUp_,     farm.reserveUpMW);
        writePoint(idxReserveDn_,     farm.reserveDownMW);
    }

protected:
    void buildPointIndexMap() override {
        registerPoint("WIND_AGC.TotalPower",       &idxTotalPower_);
        registerPoint("WIND_AGC.SchedulePower",    &idxSchedulePower_);
        registerPoint("WIND_AGC.Setpoint",          &idxSetpoint_);
        registerPoint("WIND_AGC.ErrorPU",           &idxErrorPU_);
        registerPoint("WIND_AGC.Mode",              &idxMode_);
        registerPoint("WIND_AGC.WindSpeed",         &idxWindSpeed_);
        registerPoint("WIND_AGC.FluctuationWarning", &idxFluctuation_);
        registerPoint("WIND_AGC.CycleCount",        &idxCycleCount_);
        registerPoint("GRID.Frequency",             &idxFreq_);
        registerPoint("SAFETY.CurrentMode",         &idxSafetyMode_);
        registerPoint("COMM.IsHealthy",             &idxCommHealthy_);
        registerPoint("SAFETY.FrozenPowerMW",       &idxFrozenPower_);
        registerPoint("CURTAIL.Ratio",              &idxCurtailRatio_);
        registerPoint("CURTAIL.ReserveUp",          &idxReserveUp_);
        registerPoint("CURTAIL.ReserveDown",        &idxReserveDn_);
        registerPoint("EXTREME.SubType",            &idxExtremeType_);
    }

private:
    size_t idxTotalPower_ = -1, idxSchedulePower_ = -1, idxSetpoint_ = -1;
    size_t idxErrorPU_ = -1, idxMode_ = -1, idxWindSpeed_ = -1;
    size_t idxFluctuation_ = -1, idxCycleCount_ = -1, idxFreq_ = -1;
    size_t idxSafetyMode_ = -1, idxCommHealthy_ = -1;
    size_t idxFrozenPower_ = -1, idxCurtailRatio_ = -1, idxExtremeType_ = -1;
    size_t idxReserveUp_ = -1, idxReserveDn_ = -1;
    int writeCount_ = 0;
};

} // namespace unified
