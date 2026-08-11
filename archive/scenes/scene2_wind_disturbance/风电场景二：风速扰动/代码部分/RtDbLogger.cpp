#include "RtDbLogger.h"
#include <cstdio>
#include <cstring>

extern "C" {
#include "rt_db_api.h"
}

RtDbLogger::RtDbLogger() : db_(nullptr), connected_(false) {
    idxTotalPower_  = idxTargetPower_ = idxMode_ = idxWindSpeed_ = (size_t)-1;
    idxFluctuationWarning_ = idxAvgPitch_ = idxAvgSpeed_ = (size_t)-1;
    idxTotalUp_ = idxTotalDown_ = (size_t)-1;
    for (int i = 0; i < MAX_TURB; i++) {
        idxPower_[i] = idxPitch_[i] = idxSpeed_[i] = (size_t)-1;
        idxUpMargin_[i] = idxDownMargin_[i] = (size_t)-1;
    }
}

RtDbLogger::~RtDbLogger() { cleanup(); }

bool RtDbLogger::initialize() {
    db_ = new rt_db_handle_t();
    std::memset(db_, 0, sizeof(rt_db_handle_t));

    if (!rt_db_init(db_, nullptr)) {
        std::fprintf(stderr, "[RtDbLogger] 无法连接共享内存，请先启动 rt_db_init.exe\n");
        delete db_; db_ = nullptr;
        return false;
    }
    connected_ = true;

    // 查找全场级数据点
    idxTotalPower_        = rt_db_find_index_by_id(db_, "WIND_AGC.TotalPower");
    idxTargetPower_       = rt_db_find_index_by_id(db_, "WIND_AGC.Setpoint");
    idxMode_              = rt_db_find_index_by_id(db_, "WIND_AGC.Mode");
    idxWindSpeed_         = rt_db_find_index_by_id(db_, "WIND_AGC.WindSpeed");
    idxFluctuationWarning_ = rt_db_find_index_by_id(db_, "WIND_AGC.FluctuationWarning");
    idxAvgPitch_          = rt_db_find_index_by_id(db_, "WIND_AGC.AvgPitch");
    idxAvgSpeed_          = rt_db_find_index_by_id(db_, "WIND_AGC.AvgSpeed");
    idxTotalUp_           = rt_db_find_index_by_id(db_, "WIND_AGC.FeedforwardMW");
    idxTotalDown_         = rt_db_find_index_by_id(db_, "WIND_AGC.FeedbackMW");

    // 查找风机级数据点
    for (int t = 0; t < MAX_TURB; t++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "TURBINE_%03d.Power", t);
        idxPower_[t]     = rt_db_find_index_by_id(db_, buf);
        snprintf(buf, sizeof(buf), "TURBINE_%03d.PitchAngle", t);
        idxPitch_[t]     = rt_db_find_index_by_id(db_, buf);
        snprintf(buf, sizeof(buf), "TURBINE_%03d.RotorSpeed", t);
        idxSpeed_[t]     = rt_db_find_index_by_id(db_, buf);
        snprintf(buf, sizeof(buf), "TURBINE_%03d.UpMargin", t);
        idxUpMargin_[t]  = rt_db_find_index_by_id(db_, buf);
        snprintf(buf, sizeof(buf), "TURBINE_%03d.DownMargin", t);
        idxDownMargin_[t] = rt_db_find_index_by_id(db_, buf);
    }

    int found = 0;
    if (idxTotalPower_ != (size_t)-1) found++;
    if (idxMode_        != (size_t)-1) found++;
    if (idxWindSpeed_   != (size_t)-1) found++;
    std::printf("[RtDbLogger] 共享内存连接成功 (WIND_AGC %d/8, TURBINE %d/4 ready)\n",
                found, (idxPower_[0] != (size_t)-1) ? 4 : 0);
    return true;
}

void RtDbLogger::cleanup() {
    if (db_) { rt_db_cleanup(db_); delete db_; db_ = nullptr; }
    connected_ = false;
}

void RtDbLogger::logStep(double timeSec, double windSpeed, int mode,
                          double targetPower, double totalActual,
                          double totalUp, double totalDown,
                          double avgPitch, double avgSpeed,
                          bool fluctuationWarning) {
    if (!connected_) return;
    if (idxTotalPower_  != (size_t)-1) rt_db_set_value(db_, idxTotalPower_,  totalActual, 1);
    if (idxTargetPower_ != (size_t)-1) rt_db_set_value(db_, idxTargetPower_, targetPower, 1);
    if (idxMode_        != (size_t)-1) rt_db_set_value(db_, idxMode_,        (double)mode, 1);
    if (idxWindSpeed_   != (size_t)-1) rt_db_set_value(db_, idxWindSpeed_,   windSpeed,   1);
    if (idxFluctuationWarning_ != (size_t)-1)
        rt_db_set_value(db_, idxFluctuationWarning_, fluctuationWarning ? 1.0 : 0.0, 1);
    if (idxAvgPitch_    != (size_t)-1) rt_db_set_value(db_, idxAvgPitch_,    avgPitch,    1);
    if (idxAvgSpeed_    != (size_t)-1) rt_db_set_value(db_, idxAvgSpeed_,    avgSpeed,    1);
    if (idxTotalUp_     != (size_t)-1) rt_db_set_value(db_, idxTotalUp_,     totalUp,     1);
    if (idxTotalDown_   != (size_t)-1) rt_db_set_value(db_, idxTotalDown_,   totalDown,   1);
}

void RtDbLogger::logTurbineDetail(int id, double pitch, double speed,
                                   double up, double down, double power) {
    if (!connected_ || id < 0 || id >= MAX_TURB) return;
    int i = id;
    if (idxPower_[i]     != (size_t)-1) rt_db_set_value(db_, idxPower_[i],     power, 1);
    if (idxPitch_[i]     != (size_t)-1) rt_db_set_value(db_, idxPitch_[i],     pitch, 1);
    if (idxSpeed_[i]     != (size_t)-1) rt_db_set_value(db_, idxSpeed_[i],     speed, 1);
    if (idxUpMargin_[i]  != (size_t)-1) rt_db_set_value(db_, idxUpMargin_[i],  up,    1);
    if (idxDownMargin_[i]!= (size_t)-1) rt_db_set_value(db_, idxDownMargin_[i], down,  1);
}

void RtDbLogger::logFinalSummary(double maxFluctuationMW, double suppressionEff) {
    if (!connected_) return;
    // 复用 MaxError 和 QualifiedRate 字段存放扰动场景的统计结果
    size_t idxMaxErr  = rt_db_find_index_by_id(db_, "WIND_AGC.MaxError");
    size_t idxQual    = rt_db_find_index_by_id(db_, "WIND_AGC.QualifiedRate");
    if (idxMaxErr != (size_t)-1) rt_db_set_value(db_, idxMaxErr, maxFluctuationMW, 1);
    if (idxQual   != (size_t)-1) rt_db_set_value(db_, idxQual,   suppressionEff * 100.0, 1);
    std::printf("[RtDbLogger] 扰动平抑摘要已写入 (最大波动=%.2f MW, 平抑效率=%.1f%%)\n",
                maxFluctuationMW, suppressionEff * 100.0);
}
