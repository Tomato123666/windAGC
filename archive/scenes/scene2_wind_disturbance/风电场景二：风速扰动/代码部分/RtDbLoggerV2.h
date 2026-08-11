/**
 * @file RtDbLoggerV2.h
 * @brief 场景2（风速扰动功率波动平抑）统一架构RT_DB日志记录器
 *
 * 继承 common::RtDbLoggerBase，利用统一基类的生命周期管理（连接/断连/点注册），
 * 仅通过 buildPointIndexMap() 注册本场景专属数据点，并提供与原有主程序
 * 完全兼容的便捷写入接口。
 *
 * 相比原版 RtDbLogger 的改进：
 *  - 复用基类的 registerPoint / writePoint，消除重复的点查找与写入逻辑
 *  - buildPointIndexMap() 集中声明所有数据点，便于维护
 *  - 新增 logModeChange() 专门记录模式切换（ECO <-> SUPPRESS）
 *  - 头文件自包含，无需单独的 .cpp 编译单元
 */

#pragma once

#include "../../../common/RtDbLoggerBase.h"
#include "../../../common/CommonTypes.h"

#include <cstdio>

// ============================================================================
// RtDbLoggerV2 —— 场景2专用，继承自 common::RtDbLoggerBase
// ============================================================================

class RtDbLoggerV2 : public common::RtDbLoggerBase {
public:
    /** 最大风机数量 */
    static const int MAX_TURB = 4;

    // ------------------------------------------------------------------
    // 构造 / 析构
    // ------------------------------------------------------------------

    RtDbLoggerV2()
        : common::RtDbLoggerBase("Scenario2-WindFluctuation")
    {
        resetIndices_();
    }

    /** 析构自动调用基类 cleanup() */
    ~RtDbLoggerV2() override = default;

    // ------------------------------------------------------------------
    // buildPointIndexMap —— 注册本场景所有数据点
    // ------------------------------------------------------------------

    void buildPointIndexMap() override {
        // ---- 全场级数据点 (WIND_AGC.*) ----
        registerPoint("WIND_AGC.TotalPower",        &idxTotalPower_);
        registerPoint("WIND_AGC.Setpoint",          &idxTargetPower_);
        registerPoint("WIND_AGC.Mode",              &idxMode_);
        registerPoint("WIND_AGC.WindSpeed",         &idxWindSpeed_);
        registerPoint("WIND_AGC.FluctuationWarning",&idxFluctuationWarning_);
        registerPoint("WIND_AGC.AvgPitch",          &idxAvgPitch_);
        registerPoint("WIND_AGC.AvgSpeed",          &idxAvgSpeed_);
        registerPoint("WIND_AGC.FeedforwardMW",     &idxTotalUp_);
        registerPoint("WIND_AGC.FeedbackMW",        &idxTotalDown_);

        // ---- 单机级数据点 (TURBINE_000 .. TURBINE_003) ----
        char buf[64];
        for (int t = 0; t < MAX_TURB; ++t) {
            snprintf(buf, sizeof(buf), "TURBINE_%03d.Power", t);
            registerPoint(buf, &idxPower_[t]);
            snprintf(buf, sizeof(buf), "TURBINE_%03d.PitchAngle", t);
            registerPoint(buf, &idxPitch_[t]);
            snprintf(buf, sizeof(buf), "TURBINE_%03d.RotorSpeed", t);
            registerPoint(buf, &idxSpeed_[t]);
            snprintf(buf, sizeof(buf), "TURBINE_%03d.UpMargin", t);
            registerPoint(buf, &idxUpMargin_[t]);
            snprintf(buf, sizeof(buf), "TURBINE_%03d.DownMargin", t);
            registerPoint(buf, &idxDownMargin_[t]);
        }
    }

    // ------------------------------------------------------------------
    // 便捷写入方法 —— 与原有 main.cpp 接口完全兼容
    // ------------------------------------------------------------------

    /**
     * 每个仿真步写入全场聚合数据
     *
     * @param timeSec           仿真时间 (s)
     * @param windSpeed         当前平均风速 (m/s)
     * @param mode              控制模式: 0=ECO, 1=SUPPRESS
     * @param targetPower       调度目标功率 (MW)
     * @param totalActual       全场实际出力 (MW)
     * @param totalUp           总上调裕量 (MW)
     * @param totalDown         总下调裕量 (MW)
     * @param avgPitch          全场平均桨距角 (deg)
     * @param avgSpeed          全场平均转速 (rpm)
     * @param fluctuationWarning 是否处于波动预警状态
     */
    void logStep(double timeSec, double windSpeed, int mode,
                 double targetPower, double totalActual,
                 double totalUp, double totalDown,
                 double avgPitch, double avgSpeed,
                 bool fluctuationWarning)
    {
        (void)timeSec;  // 时间戳暂存于仿真端；RT_DB 侧可通过写入序列推断
        writePoint(idxTotalPower_,         totalActual);
        writePoint(idxTargetPower_,        targetPower);
        writePoint(idxMode_,               static_cast<double>(mode));
        writePoint(idxWindSpeed_,          windSpeed);
        writePoint(idxFluctuationWarning_, fluctuationWarning ? 1.0 : 0.0);
        writePoint(idxAvgPitch_,           avgPitch);
        writePoint(idxAvgSpeed_,           avgSpeed);
        writePoint(idxTotalUp_,            totalUp);
        writePoint(idxTotalDown_,          totalDown);
    }

    /**
     * 写入单台风机详细状态
     *
     * @param id     风机编号 (0~3)
     * @param pitch  桨距角 (deg)
     * @param speed  转子转速 (rpm)
     * @param up     上调裕量 (MW)
     * @param down   下调裕量 (MW)
     * @param power  实际功率 (MW)
     */
    void logTurbineDetail(int id,
                          double pitchAngle, double rotorSpeed,
                          double upMargin, double downMargin,
                          double actualPower)
    {
        if (id < 0 || id >= MAX_TURB) return;
        writePoint(idxPower_[id],     actualPower);
        writePoint(idxPitch_[id],     pitchAngle);
        writePoint(idxSpeed_[id],     rotorSpeed);
        writePoint(idxUpMargin_[id],  upMargin);
        writePoint(idxDownMargin_[id], downMargin);
    }

    /**
     * 仿真结束时写入扰动平抑汇总统计
     *
     * 复用 WIND_AGC.MaxError（最大功率波动 MW）和
     * WIND_AGC.QualifiedRate（平抑效率 %）两个字段。
     *
     * @param maxFluctuationMW  仿真全程最大功率波动 (MW)
     * @param suppressionEff    平抑效率 (0~1 小数)
     */
    void logFinalSummary(double maxFluctuationMW, double suppressionEff) {
        if (!isConnected()) return;

        size_t idxMaxErr = rt_db_find_index_by_id(db_, "WIND_AGC.MaxError");
        size_t idxQual   = rt_db_find_index_by_id(db_, "WIND_AGC.QualifiedRate");

        writePoint(idxMaxErr, maxFluctuationMW);
        writePoint(idxQual,   suppressionEff * 100.0);

        std::printf("[%s] Fluctuation suppression summary written: "
                    "max=%.2f MW, efficiency=%.1f%%\n",
                    sceneName_, maxFluctuationMW, suppressionEff * 100.0);
    }

    /**
     * 记录控制模式切换（ECO / SUPPRESS）
     *
     * 同时写入共享内存 WIND_AGC.Mode 并在控制台打印切换信息。
     *
     * @param mode 0=ECO (经济模式), 1=SUPPRESS (波动平抑模式)
     */
    void logModeChange(int mode) {
        writePoint(idxMode_, static_cast<double>(mode));
        const char* name = (mode == 0) ? "ECO (Economic Mode)"
                                       : "SUPPRESS (Fluctuation Suppression)";
        std::printf("[%s] Control mode switched to: %s\n", sceneName_, name);
    }

private:
    // ------------------------------------------------------------------
    // 内部辅助
    // ------------------------------------------------------------------

    void resetIndices_() {
        idxTotalPower_  = idxTargetPower_ = idxMode_ = idxWindSpeed_ = (size_t)-1;
        idxFluctuationWarning_ = idxAvgPitch_ = idxAvgSpeed_ = (size_t)-1;
        idxTotalUp_ = idxTotalDown_ = (size_t)-1;
        for (int i = 0; i < MAX_TURB; ++i) {
            idxPower_[i]     = (size_t)-1;
            idxPitch_[i]     = (size_t)-1;
            idxSpeed_[i]     = (size_t)-1;
            idxUpMargin_[i]  = (size_t)-1;
            idxDownMargin_[i]= (size_t)-1;
        }
    }

    // ------------------------------------------------------------------
    // 数据点索引 —— 与 buildPointIndexMap() 注册的点一一对应
    // ------------------------------------------------------------------

    size_t idxTotalPower_          = (size_t)-1;
    size_t idxTargetPower_         = (size_t)-1;
    size_t idxMode_                = (size_t)-1;
    size_t idxWindSpeed_           = (size_t)-1;
    size_t idxFluctuationWarning_  = (size_t)-1;
    size_t idxAvgPitch_            = (size_t)-1;
    size_t idxAvgSpeed_            = (size_t)-1;
    size_t idxTotalUp_             = (size_t)-1;
    size_t idxTotalDown_           = (size_t)-1;

    size_t idxPower_[MAX_TURB];
    size_t idxPitch_[MAX_TURB];
    size_t idxSpeed_[MAX_TURB];
    size_t idxUpMargin_[MAX_TURB];
    size_t idxDownMargin_[MAX_TURB];
};
