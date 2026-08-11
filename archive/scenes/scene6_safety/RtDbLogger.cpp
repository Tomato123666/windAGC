#include "RtDbLogger.h"
#include <cstdio>
#include <cstring>

extern "C" {
#include "rt_db_api.h"
}

RtDbLogger::RtDbLogger() : db_(nullptr), connected_(false) {
    idxTotalP_ = idxSchedP_ = idxAvailP_ = idxWindS_ = idxMode_ = (size_t)-1;
    idxExtreme_ = idxComm_ = idxFrozen_ = idxVoltage_ = idxRatio_ = (size_t)-1;
}

RtDbLogger::~RtDbLogger() { cleanup(); }

bool RtDbLogger::initialize() {
    db_ = new rt_db_handle_t();
    std::memset(db_, 0, sizeof(rt_db_handle_t));
    if (!rt_db_init(db_, nullptr)) {
        std::fprintf(stderr, "[RtDbLogger-S6] 无法连接共享内存\n");
        delete db_; db_ = nullptr; return false;
    }
    connected_ = true;

    idxTotalP_  = rt_db_find_index_by_id(db_, "WIND_AGC.TotalPower");
    idxSchedP_  = rt_db_find_index_by_id(db_, "WIND_AGC.SchedulePower");
    idxAvailP_  = rt_db_find_index_by_id(db_, "WIND_AGC.FeedforwardMW");
    idxWindS_   = rt_db_find_index_by_id(db_, "WIND_AGC.WindSpeed");
    idxMode_    = rt_db_find_index_by_id(db_, "WIND_AGC.Mode");
    idxExtreme_ = rt_db_find_index_by_id(db_, "WIND_AGC.FluctuationWarning");
    idxComm_    = rt_db_find_index_by_id(db_, "WIND_AGC.ErrorPU");
    idxFrozen_  = rt_db_find_index_by_id(db_, "WIND_AGC.Setpoint");
    idxVoltage_ = rt_db_find_index_by_id(db_, "GRID.Frequency");
    idxRatio_   = rt_db_find_index_by_id(db_, "WIND_AGC.QualifiedRate");

    std::printf("[RtDbLogger-S6] 共享内存连接成功\n");
    return true;
}

void RtDbLogger::cleanup() {
    if (db_) { rt_db_cleanup(db_); delete db_; db_ = nullptr; }
    connected_ = false;
}

void RtDbLogger::logState(int hour, int minute, float totalPower,
                           float schedulePower, float availPower,
                           float windSpeed, float turbulence,
                           int safetyMode, int extremeType, int commStatus,
                           float frozenPower, float voltage, float curtailRatio) {
    if (!connected_) return;
    if (idxTotalP_  != (size_t)-1) rt_db_set_value(db_, idxTotalP_,  totalPower, 1);
    if (idxSchedP_  != (size_t)-1) rt_db_set_value(db_, idxSchedP_,  schedulePower, 1);
    if (idxAvailP_  != (size_t)-1) rt_db_set_value(db_, idxAvailP_,  availPower, 1);
    if (idxWindS_   != (size_t)-1) rt_db_set_value(db_, idxWindS_,   windSpeed, 1);
    if (idxMode_    != (size_t)-1) rt_db_set_value(db_, idxMode_,    (double)safetyMode, 1);
    if (idxExtreme_ != (size_t)-1) rt_db_set_value(db_, idxExtreme_, (double)extremeType, 1);
    if (idxComm_    != (size_t)-1) rt_db_set_value(db_, idxComm_,    (double)commStatus, 1);
    if (idxFrozen_  != (size_t)-1) rt_db_set_value(db_, idxFrozen_,  frozenPower, 1);
    if (idxVoltage_ != (size_t)-1) rt_db_set_value(db_, idxVoltage_, voltage, 1);
    if (idxRatio_   != (size_t)-1) rt_db_set_value(db_, idxRatio_,   curtailRatio * 100.0f, 1);
}

void RtDbLogger::logSafetyEvent(const char* event, float value) {
    if (!connected_) return;
    std::printf("[RtDbLogger-S6] 安全事件: %s (%.1f)\n", event, value);
}

void RtDbLogger::logCommEvent(bool healthy, int hour, int minute) {
    std::printf("[RtDbLogger-S6] 通信事件: %02d:%02d → %s\n",
                hour, minute, healthy ? "恢复" : "中断");
}

void RtDbLogger::logExtremeEvent(int subType, float windSpeed, float turbulence) {
    const char* names[] = {"无", "切出风速", "高湍流", "风暴穿越"};
    std::printf("[RtDbLogger-S6] 极端天气: %s (风%.1fm/s 湍%.2f)\n",
                names[subType], windSpeed, turbulence);
}
