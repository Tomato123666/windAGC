/**
 * @file RtDbLoggerV2.h
 * @brief Scenario 1 AGC Economic Dispatch Logger — unified architecture adapter
 *
 * Inherits from common::RtDbLoggerBase to share RT_DB lifecycle management
 * across all scenarios. Registers 311 data points (11 plant-level + 300
 * turbine-level for turbines 000~099) and exposes the same convenience
 * interface as the original RtDbLogger so main.cpp can switch with a
 * one-line type change.
 *
 * Usage in main.cpp:
 *   #include "RtDbLoggerV2.h"    // was: #include "RtDbLogger.h"
 *   RtDbLoggerV2 logger;         // was: RtDbLogger logger;
 *   // everything else stays the same
 *
 * Prerequisite: #include "Turbine.h" before this header in the translation
 * unit that calls logTurbineStates().
 */

#pragma once

#include "../../common/RtDbLoggerBase.h"
#include "../../common/CommonTypes.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

// Forward declaration — the full Turbine definition (from "Turbine.h") must
// be visible in any translation unit that calls logTurbineStates().
class Turbine;

/**
 * RtDbLoggerV2
 *
 * Scenario 1 data logger built on the common::RtDbLoggerBase foundation.
 *
 * Data points (311 total):
 *   WIND_AGC.TotalPower      — 全场实际总功率 (MW)
 *   WIND_AGC.SchedulePower   — 调度计划功率 (MW)
 *   WIND_AGC.Setpoint        — 全场功率指令 (MW)
 *   WIND_AGC.ErrorPU         — 跟踪误差 (pu, 以额定容量为基值)
 *   WIND_AGC.FeedforwardMW   — 前馈分量 (MW)
 *   WIND_AGC.FeedbackMW      — 反馈分量 (MW)
 *   WIND_AGC.MAE             — 平均绝对误差 (%)
 *   WIND_AGC.RMSE            — 均方根误差 (%)
 *   WIND_AGC.QualifiedRate   — 合格率 (%)
 *   WIND_AGC.MaxError        — 最大误差 (%)
 *   WIND_AGC.CycleCount      — 控制周期计数
 *   TURBINE_000~099.Power    — 各风机有功功率 (MW)
 *   TURBINE_000~099.WindSpeed— 各风机风速 (m/s)
 *   TURBINE_000~099.Command  — 各风机功率指令 (MW)
 */
class RtDbLoggerV2 : public common::RtDbLoggerBase {
public:
    /** Maximum number of turbines whose state is logged each cycle. */
    static constexpr int maxTurbinesToLog_ = 100;

    // ------------------------------------------------------------------
    // Construction / destruction
    // ------------------------------------------------------------------

    /**
     * Constructor — initialises all point indices to invalid.
     * The scene name "Scenario1" is passed to the base for log tagging.
     */
    RtDbLoggerV2()
        : RtDbLoggerBase("Scenario1")
    {
        idxTotalPower_    = (size_t)-1;
        idxSchedulePower_ = (size_t)-1;
        idxSetpoint_      = (size_t)-1;
        idxErrorPU_       = (size_t)-1;
        idxFeedforwardMW_ = (size_t)-1;
        idxFeedbackMW_    = (size_t)-1;
        idxMAE_           = (size_t)-1;
        idxRMSE_          = (size_t)-1;
        idxQualifiedRate_ = (size_t)-1;
        idxMaxError_      = (size_t)-1;
        idxCycleCount_    = (size_t)-1;

        for (int i = 0; i < maxTurbinesToLog_; i++) {
            idxTurbinePower_[i]     = (size_t)-1;
            idxTurbineWindSpeed_[i] = (size_t)-1;
            idxTurbineCommand_[i]   = (size_t)-1;
        }
    }

    // Destructor, initialize(), cleanup(), and isConnected() are inherited
    // from common::RtDbLoggerBase — no override needed.

    // ------------------------------------------------------------------
    // Accessors
    // ------------------------------------------------------------------

    /** Returns the maximum number of turbines logged per cycle. */
    int getMaxTurbinesToLog() const { return maxTurbinesToLog_; }

    // ------------------------------------------------------------------
    // Convenience logging methods (identical signatures to RtDbLogger)
    // ------------------------------------------------------------------

    /**
     * Write plant-level operating data for one control cycle.
     *
     * @param currentTimeSec   Simulation / wall-clock time (unused in this
     *                         implementation but kept for interface parity)
     * @param schedulePower    Dispatch schedule target (MW)
     * @param totalActualPower Measured total farm output (MW)
     * @param setpoint         Computed power setpoint sent to turbines (MW)
     * @param errorPU          Tracking error in per-unit (schedule - actual) / Pn
     * @param feedforwardMW    Feedforward control component (MW)
     * @param feedbackMW       Feedback (PID) control component (MW)
     * @param cycleCount       Monotonically increasing cycle counter
     */
    void logControlCycle(double /*currentTimeSec*/,
                         double schedulePower,
                         double totalActualPower,
                         double setpoint,
                         double errorPU,
                         double feedforwardMW,
                         double feedbackMW,
                         int cycleCount)
    {
        if (!connected_) return;

        writePoint(idxTotalPower_,    totalActualPower);
        writePoint(idxSchedulePower_, schedulePower);
        writePoint(idxSetpoint_,      setpoint);
        writePoint(idxErrorPU_,       errorPU);
        writePoint(idxFeedforwardMW_, feedforwardMW);
        writePoint(idxFeedbackMW_,    feedbackMW);
        writePoint(idxCycleCount_,    static_cast<double>(cycleCount));
    }

    /**
     * Write per-turbine power, wind speed, and command for up to @p count
     * turbines.  Turbines beyond maxTurbinesToLog_ are silently clamped.
     *
     * @param turbines   Vector of shared_ptr<Turbine> — requires full Turbine
     *                   definition in the including translation unit.
     * @param windSpeeds Array of wind speeds indexed by turbine ID (may be null).
     * @param count      Number of turbines to write (0 .. maxTurbinesToLog_).
     */
    void logTurbineStates(const std::vector<std::shared_ptr<Turbine>>& turbines,
                          const double* windSpeeds,
                          int count)
    {
        if (!connected_) return;

        const int n = (count < maxTurbinesToLog_) ? count : maxTurbinesToLog_;
        for (int i = 0; i < n; i++) {
            if (idxTurbinePower_[i] != (size_t)-1)
                writePoint(idxTurbinePower_[i],
                           turbines[i]->getActualPower());

            if (idxTurbineWindSpeed_[i] != (size_t)-1 && windSpeeds != nullptr)
                writePoint(idxTurbineWindSpeed_[i],
                           windSpeeds[turbines[i]->getId()]);

            if (idxTurbineCommand_[i] != (size_t)-1)
                writePoint(idxTurbineCommand_[i],
                           turbines[i]->getActualPower());
        }
    }

    /**
     * Write end-of-simulation performance statistics.
     *
     * Values are scaled to percentages (x100) before writing to match the
     * HMI expectation established by the original logger.
     *
     * @param mae           Mean Absolute Error (raw, e.g. 0.03 for 3 %)
     * @param rmse          Root Mean Square Error (raw)
     * @param maxError      Maximum single-cycle error (raw)
     * @param qualifiedRate Qualification rate (raw, e.g. 0.95 for 95 %)
     * @param totalCycles   Total control cycles executed
     */
    void logFinalStats(double mae, double rmse, double maxError,
                       double qualifiedRate, int totalCycles)
    {
        if (!connected_) return;

        writePoint(idxMAE_,           mae * 100.0);
        writePoint(idxRMSE_,          rmse * 100.0);
        writePoint(idxMaxError_,      maxError * 100.0);
        writePoint(idxQualifiedRate_, qualifiedRate * 100.0);
        writePoint(idxCycleCount_,    static_cast<double>(totalCycles));

        std::printf("[%s] Final statistics written to shared memory\n",
                    sceneName_);
    }

    /**
     * Non-blocking poll of the dispatch command queue.
     *
     * @param[out] targetPowerMW  Target power in MW if a new command is available.
     * @param[out] commandType    Command type code (see CommandType enum).
     * @return true if a command was dequeued, false otherwise.
     */
    bool checkDispatchCommand(double& targetPowerMW, int& commandType)
    {
        if (!connected_) return false;

        ControlCommand cmd;
        if (popCommand(&cmd)) {
            targetPowerMW = cmd.value;
            commandType   = cmd.command_type;
            std::printf("[%s] Dispatch command: device=%s type=%d "
                        "target=%.1f MW priority=%d\n",
                        sceneName_, cmd.device_id, cmd.command_type,
                        cmd.value, cmd.priority);
            return true;
        }
        return false;
    }

protected:
    // ------------------------------------------------------------------
    // Point index mapping (called once by base-class initialize())
    // ------------------------------------------------------------------

    /**
     * Register all 311 Scenario-1 data points with the RT_DB handle.
     *
     * Each call to registerPoint() looks up the point ID in the shared-
     * memory table and stores its index.  Missing points are flagged with
     * a warning but do not prevent the logger from operating (the
     * corresponding writePoint() calls become no-ops).
     */
    void buildPointIndexMap() override
    {
        // ---- Plant-level (11 points) -----------------------------------
        registerPoint("WIND_AGC.TotalPower",    &idxTotalPower_);
        registerPoint("WIND_AGC.SchedulePower", &idxSchedulePower_);
        registerPoint("WIND_AGC.Setpoint",      &idxSetpoint_);
        registerPoint("WIND_AGC.ErrorPU",       &idxErrorPU_);
        registerPoint("WIND_AGC.FeedforwardMW", &idxFeedforwardMW_);
        registerPoint("WIND_AGC.FeedbackMW",    &idxFeedbackMW_);
        registerPoint("WIND_AGC.MAE",           &idxMAE_);
        registerPoint("WIND_AGC.RMSE",          &idxRMSE_);
        registerPoint("WIND_AGC.QualifiedRate", &idxQualifiedRate_);
        registerPoint("WIND_AGC.MaxError",      &idxMaxError_);
        registerPoint("WIND_AGC.CycleCount",    &idxCycleCount_);

        // ---- Turbine-level (100 turbines x 3 = 300 points) -------------
        for (int i = 0; i < maxTurbinesToLog_; i++) {
            char buf[64];

            std::snprintf(buf, sizeof(buf), "TURBINE_%03d.Power", i);
            registerPoint(buf, &idxTurbinePower_[i]);

            std::snprintf(buf, sizeof(buf), "TURBINE_%03d.WindSpeed", i);
            registerPoint(buf, &idxTurbineWindSpeed_[i]);

            std::snprintf(buf, sizeof(buf), "TURBINE_%03d.Command", i);
            registerPoint(buf, &idxTurbineCommand_[i]);
        }

        // ---- Registration summary --------------------------------------
        int plantFound = 0;
        if (idxTotalPower_    != (size_t)-1) plantFound++;
        if (idxSchedulePower_ != (size_t)-1) plantFound++;
        if (idxSetpoint_      != (size_t)-1) plantFound++;
        if (idxErrorPU_       != (size_t)-1) plantFound++;
        if (idxFeedforwardMW_ != (size_t)-1) plantFound++;
        if (idxFeedbackMW_    != (size_t)-1) plantFound++;
        if (idxMAE_           != (size_t)-1) plantFound++;
        if (idxRMSE_          != (size_t)-1) plantFound++;
        if (idxQualifiedRate_ != (size_t)-1) plantFound++;
        if (idxMaxError_      != (size_t)-1) plantFound++;
        if (idxCycleCount_    != (size_t)-1) plantFound++;

        int turbineFound = 0;
        for (int i = 0; i < maxTurbinesToLog_; i++) {
            if (idxTurbinePower_[i] != (size_t)-1) turbineFound++;
        }

        std::printf("[%s] Point map: plant %d/11  turbine-power %d/%d  "
                    "total-mapped %zu\n",
                    sceneName_, plantFound, turbineFound,
                    maxTurbinesToLog_, pointCount_);
    }

private:
    // ====================================================================
    // Plant-level point indices (11)
    // ====================================================================
    size_t idxTotalPower_;
    size_t idxSchedulePower_;
    size_t idxSetpoint_;
    size_t idxErrorPU_;
    size_t idxFeedforwardMW_;
    size_t idxFeedbackMW_;
    size_t idxMAE_;
    size_t idxRMSE_;
    size_t idxQualifiedRate_;
    size_t idxMaxError_;
    size_t idxCycleCount_;

    // ====================================================================
    // Turbine-level point indices (100 turbines x 3 points = 300)
    // ====================================================================
    size_t idxTurbinePower_[100];
    size_t idxTurbineWindSpeed_[100];
    size_t idxTurbineCommand_[100];
};
