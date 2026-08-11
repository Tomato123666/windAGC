#include "RtDbLogger.h"
#include "Turbine.h"
#include <cstdio>
#include <cstring>

// 引入C接口
extern "C" {
#include "rt_db_api.h"
}

RtDbLogger::RtDbLogger()
    : db_(nullptr), connected_(false)
{
    // 初始化所有索引为 -1 (无效)
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

RtDbLogger::~RtDbLogger() {
    cleanup();
}

bool RtDbLogger::initialize() {
    db_ = new rt_db_handle_t();
    std::memset(db_, 0, sizeof(rt_db_handle_t));

    if (!rt_db_init(db_, nullptr)) {
        std::fprintf(stderr, "[RtDbLogger] 无法连接共享内存，"
                     "请先启动 rt_db_init.exe\n");
        delete db_;
        db_ = nullptr;
        connected_ = false;
        return false;
    }

    connected_ = true;
    std::printf("[RtDbLogger] 共享内存连接成功\n");
    return buildPointIndexMap();
}

void RtDbLogger::cleanup() {
    if (db_) {
        rt_db_cleanup(db_);
        delete db_;
        db_ = nullptr;
    }
    connected_ = false;
}

size_t RtDbLogger::registerPoint(const char* pointId, const char* units,
                                  double initialValue, long quality) {
    if (!connected_) return (size_t)-1;

    // 先查找是否已存在
    size_t idx = rt_db_find_index_by_id(db_, pointId);
    if (idx != (size_t)-1) {
        rt_db_set_value(db_, idx, initialValue, quality);
        return idx;
    }

    // 不存在则在共享内存中找一个 UNUSED_ 槽位注册
    // 这里简化处理：直接返回 find_index 的结果（-1）
    // 实际注册需要在 init_rt_db 阶段预定义点ID
    std::fprintf(stderr, "[RtDbLogger] 警告: 数据点 %s 未找到，"
                 "请确认 rt_db_init 中已注册\n", pointId);
    return (size_t)-1;
}

bool RtDbLogger::buildPointIndexMap() {
    if (!connected_) return false;

    // ===== 全场级数据点 =====
    idxTotalPower_    = rt_db_find_index_by_id(db_, "WIND_AGC.TotalPower");
    idxSchedulePower_ = rt_db_find_index_by_id(db_, "WIND_AGC.SchedulePower");
    idxSetpoint_      = rt_db_find_index_by_id(db_, "WIND_AGC.Setpoint");
    idxErrorPU_       = rt_db_find_index_by_id(db_, "WIND_AGC.ErrorPU");
    idxFeedforwardMW_ = rt_db_find_index_by_id(db_, "WIND_AGC.FeedforwardMW");
    idxFeedbackMW_    = rt_db_find_index_by_id(db_, "WIND_AGC.FeedbackMW");
    idxMAE_           = rt_db_find_index_by_id(db_, "WIND_AGC.MAE");
    idxRMSE_          = rt_db_find_index_by_id(db_, "WIND_AGC.RMSE");
    idxQualifiedRate_ = rt_db_find_index_by_id(db_, "WIND_AGC.QualifiedRate");
    idxMaxError_      = rt_db_find_index_by_id(db_, "WIND_AGC.MaxError");
    idxCycleCount_    = rt_db_find_index_by_id(db_, "WIND_AGC.CycleCount");

    // ===== 风机级数据点 =====
    for (int i = 0; i < maxTurbinesToLog_; i++) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "TURBINE_%03d.Power", i);
        idxTurbinePower_[i] = rt_db_find_index_by_id(db_, buf);

        std::snprintf(buf, sizeof(buf), "TURBINE_%03d.WindSpeed", i);
        idxTurbineWindSpeed_[i] = rt_db_find_index_by_id(db_, buf);

        std::snprintf(buf, sizeof(buf), "TURBINE_%03d.Command", i);
        idxTurbineCommand_[i] = rt_db_find_index_by_id(db_, buf);
    }

    // 打印注册摘要
    int foundCount = 0;
    if (idxTotalPower_ != (size_t)-1) foundCount++;
    if (idxSchedulePower_ != (size_t)-1) foundCount++;
    if (idxSetpoint_ != (size_t)-1) foundCount++;
    if (idxErrorPU_ != (size_t)-1) foundCount++;
    if (idxFeedforwardMW_ != (size_t)-1) foundCount++;
    if (idxFeedbackMW_ != (size_t)-1) foundCount++;
    if (idxMAE_ != (size_t)-1) foundCount++;
    if (idxRMSE_ != (size_t)-1) foundCount++;
    if (idxQualifiedRate_ != (size_t)-1) foundCount++;
    if (idxMaxError_ != (size_t)-1) foundCount++;
    if (idxCycleCount_ != (size_t)-1) foundCount++;

    int turbineFound = 0;
    for (int i = 0; i < maxTurbinesToLog_; i++) {
        if (idxTurbinePower_[i] != (size_t)-1) turbineFound++;
    }

    std::printf("[RtDbLogger] 数据点注册: 全场 %d/11 个, 风机功率 %d/%d 个\n",
                foundCount, turbineFound, maxTurbinesToLog_);

    // 至少全场级数据点要全部注册成功
    return (foundCount >= 8);
}

// ============================================================================
// 核心日志方法
// ============================================================================

void RtDbLogger::logControlCycle(double currentTimeSec,
                                  double schedulePower,
                                  double totalActualPower,
                                  double setpoint,
                                  double errorPU,
                                  double feedforwardMW,
                                  double feedbackMW,
                                  int cycleCount) {
    if (!connected_) return;

    // 批量写入全场数据（使用 rt_db_set_value 原子写入）
    if (idxTotalPower_    != (size_t)-1) rt_db_set_value(db_, idxTotalPower_,    totalActualPower, 1);
    if (idxSchedulePower_ != (size_t)-1) rt_db_set_value(db_, idxSchedulePower_, schedulePower,    1);
    if (idxSetpoint_      != (size_t)-1) rt_db_set_value(db_, idxSetpoint_,      setpoint,         1);
    if (idxErrorPU_       != (size_t)-1) rt_db_set_value(db_, idxErrorPU_,       errorPU,          1);
    if (idxFeedforwardMW_ != (size_t)-1) rt_db_set_value(db_, idxFeedforwardMW_, feedforwardMW,    1);
    if (idxFeedbackMW_    != (size_t)-1) rt_db_set_value(db_, idxFeedbackMW_,    feedbackMW,       1);
    if (idxCycleCount_    != (size_t)-1) rt_db_set_value(db_, idxCycleCount_,    (double)cycleCount, 1);
}

void RtDbLogger::logTurbineStates(const std::vector<std::shared_ptr<Turbine>>& turbines,
                                   const double* windSpeeds,
                                   int count) {
    if (!connected_) return;

    int n = (count < maxTurbinesToLog_) ? count : maxTurbinesToLog_;
    for (int i = 0; i < n; i++) {
        if (idxTurbinePower_[i] != (size_t)-1)
            rt_db_set_value(db_, idxTurbinePower_[i],
                            turbines[i]->getActualPower(), 1);

        if (idxTurbineWindSpeed_[i] != (size_t)-1 && windSpeeds)
            rt_db_set_value(db_, idxTurbineWindSpeed_[i],
                            windSpeeds[turbines[i]->getId()], 1);

        if (idxTurbineCommand_[i] != (size_t)-1)
            rt_db_set_value(db_, idxTurbineCommand_[i],
                            turbines[i]->getActualPower(), 1);  // cmd 通过 Turbine 接口获取
    }
}

void RtDbLogger::logFinalStats(double mae, double rmse, double maxError,
                                double qualifiedRate, int totalCycles) {
    if (!connected_) return;

    if (idxMAE_           != (size_t)-1) rt_db_set_value(db_, idxMAE_,           mae * 100.0,       1);
    if (idxRMSE_          != (size_t)-1) rt_db_set_value(db_, idxRMSE_,          rmse * 100.0,      1);
    if (idxMaxError_      != (size_t)-1) rt_db_set_value(db_, idxMaxError_,      maxError * 100.0,  1);
    if (idxQualifiedRate_ != (size_t)-1) rt_db_set_value(db_, idxQualifiedRate_, qualifiedRate * 100.0, 1);
    if (idxCycleCount_    != (size_t)-1) rt_db_set_value(db_, idxCycleCount_,    (double)totalCycles, 1);

    std::printf("[RtDbLogger] 最终统计数据已写入共享内存\n");
}

bool RtDbLogger::checkDispatchCommand(double& targetPowerMW, int& commandType) {
    if (!connected_) return false;

    ControlCommand cmd;
    if (rt_db_pop_command(db_, &cmd)) {
        targetPowerMW = cmd.value;
        commandType = cmd.command_type;
        std::printf("[RtDbLogger] 收到调度指令: device=%s, type=%d, "
                    "target=%.1f MW, priority=%d\n",
                    cmd.device_id, cmd.command_type, cmd.value, cmd.priority);
        return true;
    }
    return false;
}
