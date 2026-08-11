#include "RtDbLogger.h"
#include <cstdio>
#include <cstring>

extern "C" {
#include "rt_db_api.h"
}

RtDbLogger::RtDbLogger() : db_(nullptr), connected_(false) {
    idxTotalPower_ = idxSetpoint_ = idxErrorPU_ = (size_t)-1;
    idxMode_ = idxFeedforwardMW_ = idxCycleCount_ = (size_t)-1;
    for (int i = 0; i < MAX_TURB; i++) {
        idxPower_[i] = idxCommand_[i] = idxAvailable_[i] = (size_t)-1;
    }
}

RtDbLogger::~RtDbLogger() { cleanup(); }

bool RtDbLogger::initialize() {
    db_ = new rt_db_handle_t();
    std::memset(db_, 0, sizeof(rt_db_handle_t));

    if (!rt_db_init(db_, nullptr)) {
        std::fprintf(stderr, "[RtDbLogger-Dispatch] 无法连接共享内存\n");
        delete db_; db_ = nullptr; return false;
    }
    connected_ = true;

    idxTotalPower_    = rt_db_find_index_by_id(db_, "WIND_AGC.TotalPower");
    idxSetpoint_      = rt_db_find_index_by_id(db_, "WIND_AGC.Setpoint");
    idxErrorPU_       = rt_db_find_index_by_id(db_, "WIND_AGC.ErrorPU");
    idxMode_          = rt_db_find_index_by_id(db_, "WIND_AGC.Mode");
    idxFeedforwardMW_ = rt_db_find_index_by_id(db_, "WIND_AGC.FeedforwardMW");
    idxCycleCount_    = rt_db_find_index_by_id(db_, "WIND_AGC.CycleCount");

    for (int t = 0; t < MAX_TURB; t++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "TURBINE_%03d.Power", t);
        idxPower_[t]     = rt_db_find_index_by_id(db_, buf);
        snprintf(buf, sizeof(buf), "TURBINE_%03d.Command", t);
        idxCommand_[t]   = rt_db_find_index_by_id(db_, buf);
        snprintf(buf, sizeof(buf), "TURBINE_%03d.WindSpeed", t);
        idxAvailable_[t] = rt_db_find_index_by_id(db_, buf); // 复用WindSpeed存可用功率
    }

    int found = 0;
    if (idxTotalPower_ != (size_t)-1) found++;
    if (idxSetpoint_   != (size_t)-1) found++;
    if (idxMode_       != (size_t)-1) found++;
    std::printf("[RtDbLogger-Dispatch] 共享内存连接成功 (%d/6 WIND_AGC, "
                "指令队列ready)\n", found);
    return (found >= 4);
}

void RtDbLogger::cleanup() {
    if (db_) { rt_db_cleanup(db_); delete db_; db_ = nullptr; }
    connected_ = false;
}

void RtDbLogger::logStep(int step, float totalPowerMW, float targetPowerMW,
                          float errorMW, int controlMode, float compensation) {
    if (!connected_) return;
    if (idxTotalPower_    != (size_t)-1) rt_db_set_value(db_, idxTotalPower_,    totalPowerMW, 1);
    if (idxSetpoint_      != (size_t)-1) rt_db_set_value(db_, idxSetpoint_,      targetPowerMW, 1);
    if (idxErrorPU_       != (size_t)-1) rt_db_set_value(db_, idxErrorPU_,       errorMW, 1);
    if (idxMode_          != (size_t)-1) rt_db_set_value(db_, idxMode_,          (double)controlMode, 1);
    if (idxFeedforwardMW_ != (size_t)-1) rt_db_set_value(db_, idxFeedforwardMW_, compensation, 1);
    if (idxCycleCount_    != (size_t)-1) rt_db_set_value(db_, idxCycleCount_,    (double)step, 1);
}

void RtDbLogger::logTurbineState(int turbineId, float powerMW, float commandMW,
                                  float availableMax, int state) {
    if (!connected_ || turbineId < 0 || turbineId >= MAX_TURB) return;
    int i = turbineId;
    if (idxPower_[i]     != (size_t)-1) rt_db_set_value(db_, idxPower_[i],     powerMW, 1);
    if (idxCommand_[i]   != (size_t)-1) rt_db_set_value(db_, idxCommand_[i],   commandMW, 1);
    if (idxAvailable_[i] != (size_t)-1) rt_db_set_value(db_, idxAvailable_[i], availableMax, 1);
}

// ====== 核心: 指令队列集成 ======

bool RtDbLogger::pollDispatchCommand(float& targetMW, int& cmdType,
                                      float& rampRate, uint8_t& priority) {
    if (!connected_) return false;

    ControlCommand cmd;
    if (rt_db_pop_command(db_, &cmd)) {
        targetMW = (float)cmd.value;
        cmdType  = cmd.command_type;
        // rampRate 和 priority 从 command_type 推导:
        // type=1 → STEP, type=2 → RAMP
        rampRate = (cmdType == 2) ? 5.0f : 0.0f;   // 默认爬坡率 5 MW/min
        priority = (uint8_t)cmd.priority;
        std::printf("[RtDbLogger-Dispatch] <<< 收到外部调度指令: device=%s, "
                    "target=%.1f MW, type=%s, priority=%d\n",
                    cmd.device_id, cmd.value,
                    (cmdType == 1) ? "STEP" : (cmdType == 2) ? "RAMP" : "OTHER",
                    cmd.priority);
        return true;
    }
    return false;
}

void RtDbLogger::logCommandAccepted(uint32_t cmdId, float targetMW, int cmdType) {
    if (!connected_) return;
    std::printf("[RtDbLogger-Dispatch] 指令已接受: id=%u, target=%.1f MW, "
                "type=%s\n", cmdId, targetMW,
                (cmdType == 0) ? "STEP" : "RAMP");
}

void RtDbLogger::logTrackingComplete(float finalPower, float finalError) {
    if (!connected_) return;
    // 将跟踪完成事件写入共享内存
    if (idxErrorPU_ != (size_t)-1) rt_db_set_value(db_, idxErrorPU_, finalError, 1);
    std::printf("[RtDbLogger-Dispatch] 跟踪完成: final=%.2f MW, error=%.2f MW\n",
                finalPower, finalError);
}
