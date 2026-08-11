#pragma once
#include <cstddef>

struct rt_db_handle;
typedef struct rt_db_handle rt_db_handle_t;

/**
 * 场景2专用 RT_DB 日志记录器
 * 风速扰动 → 波动平抑模式 → 共享内存实时监控
 */
class RtDbLogger {
public:
    RtDbLogger();
    ~RtDbLogger();

    bool initialize();
    void cleanup();

    // 每个仿真步调用
    void logStep(double timeSec, double windSpeed, int mode,
                 double targetPower, double totalActual,
                 double totalUp, double totalDown,
                 double avgPitch, double avgSpeed,
                 bool fluctuationWarning);

    // 写入单台风机详细状态（桨距角/转速/裕量）
    void logTurbineDetail(int turbineId, double pitchAngle, double rotorSpeed,
                          double upMargin, double downMargin, double actualPower);

    // 写入最终摘要
    void logFinalSummary(double maxFluctuationMW, double suppressionEfficiency);

private:
    rt_db_handle_t* db_;
    bool connected_;

    // WIND_AGC 索引
    size_t idxTotalPower_, idxTargetPower_, idxMode_, idxWindSpeed_;
    size_t idxFluctuationWarning_, idxAvgPitch_, idxAvgSpeed_;
    size_t idxTotalUp_, idxTotalDown_;

    // 风机索引 (最多4台)
    static const int MAX_TURB = 4;
    size_t idxPower_[4], idxPitch_[4], idxSpeed_[4];
    size_t idxUpMargin_[4], idxDownMargin_[4];
};
