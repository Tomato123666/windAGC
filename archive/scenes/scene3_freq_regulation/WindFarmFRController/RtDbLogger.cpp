#include "RtDbLogger.h"
#include <cstdio>
#include <cstring>

extern "C" {
#include "rt_db_api.h"
}

RtDbLogger::RtDbLogger() : db_(nullptr), connected_(false) {
    idxFrequency_ = idxFreqDelta_ = idxState_ = idxTotalFR_ = (size_t)-1;
    idxDeltaF_ = idxProp_ = idxInteg_ = (size_t)-1;
    for (int i = 0; i < MAX_TURB; i++) {
        idxTurbinePower_[i] = idxTurbineFR_[i] = idxTurbineCmd_[i] = (size_t)-1;
    }
}

RtDbLogger::~RtDbLogger() { cleanup(); }

bool RtDbLogger::initialize() {
    db_ = new rt_db_handle_t();
    std::memset(db_, 0, sizeof(rt_db_handle_t));

    if (!rt_db_init(db_, nullptr)) {
        std::fprintf(stderr, "[RtDbLogger] 无法连接共享内存\n");
        delete db_; db_ = nullptr;
        return false;
    }
    connected_ = true;

    // GRID 数据点
    idxFrequency_  = rt_db_find_index_by_id(db_, "GRID.Frequency");
    idxFreqDelta_  = rt_db_find_index_by_id(db_, "GRID.FrequencyDelta");

    // FR_CTRL 数据点
    idxState_      = rt_db_find_index_by_id(db_, "FR_CTRL.State");
    idxTotalFR_    = rt_db_find_index_by_id(db_, "FR_CTRL.TotalFRPower");
    idxDeltaF_     = rt_db_find_index_by_id(db_, "FR_CTRL.DeltaF");
    idxProp_       = rt_db_find_index_by_id(db_, "FR_CTRL.Proportional");
    idxInteg_      = rt_db_find_index_by_id(db_, "FR_CTRL.Integral");

    // 风机数据点 (前10台)
    for (int t = 0; t < MAX_TURB; t++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "TURBINE_%03d.Power", t);
        idxTurbinePower_[t] = rt_db_find_index_by_id(db_, buf);
        snprintf(buf, sizeof(buf), "TURBINE_%03d.FR_Power", t);
        idxTurbineFR_[t]    = rt_db_find_index_by_id(db_, buf);
        snprintf(buf, sizeof(buf), "TURBINE_%03d.Command", t);
        idxTurbineCmd_[t]   = rt_db_find_index_by_id(db_, buf);
    }

    int found = 0;
    if (idxFrequency_ != (size_t)-1) found++;
    if (idxState_     != (size_t)-1) found++;
    if (idxTotalFR_   != (size_t)-1) found++;
    std::printf("[RtDbLogger-FR] 共享内存连接成功 (GRID+FR_CTRL %d/7 ready)\n", found);
    return (found >= 5);
}

void RtDbLogger::cleanup() {
    if (db_) { rt_db_cleanup(db_); delete db_; db_ = nullptr; }
    connected_ = false;
}

void RtDbLogger::logFRCycle(double frequency, double deltaF, double df_dt,
                             int state, double totalFRPower,
                             double proportionalKW, double integralKW) {
    if (!connected_) return;
    if (idxFrequency_ != (size_t)-1) rt_db_set_value(db_, idxFrequency_, frequency, 1);
    if (idxFreqDelta_ != (size_t)-1) rt_db_set_value(db_, idxFreqDelta_, deltaF, 1);
    if (idxState_     != (size_t)-1) rt_db_set_value(db_, idxState_,     (double)state, 1);
    if (idxTotalFR_   != (size_t)-1) rt_db_set_value(db_, idxTotalFR_,   totalFRPower, 1);
    if (idxDeltaF_    != (size_t)-1) rt_db_set_value(db_, idxDeltaF_,    deltaF, 1);
    if (idxProp_      != (size_t)-1) rt_db_set_value(db_, idxProp_,      proportionalKW, 1);
    if (idxInteg_     != (size_t)-1) rt_db_set_value(db_, idxInteg_,     integralKW, 1);
}

void RtDbLogger::logTurbineFR(int turbineId, double basePower, double frPower,
                               double finalPower, bool running) {
    if (!connected_ || turbineId < 1 || turbineId > MAX_TURB) return;
    int i = turbineId - 1; // turbineId is 1-based
    if (idxTurbinePower_[i] != (size_t)-1) rt_db_set_value(db_, idxTurbinePower_[i], basePower, 1);
    if (idxTurbineFR_[i]    != (size_t)-1) rt_db_set_value(db_, idxTurbineFR_[i],    frPower,   1);
    if (idxTurbineCmd_[i]   != (size_t)-1) rt_db_set_value(db_, idxTurbineCmd_[i],   finalPower, 1);
}

void RtDbLogger::logDisturbance(double triggerFreq) {
    if (!connected_) return;
    if (idxFrequency_ != (size_t)-1) rt_db_set_value(db_, idxFrequency_, triggerFreq, 1);
    std::printf("[RtDbLogger-FR] 频率扰动事件已记录: %.2f Hz\n", triggerFreq);
}
