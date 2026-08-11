#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <memory>
#include <string>

// 前向声明 (C接口, 用 extern "C" 链接)
struct rt_db_handle;
typedef struct rt_db_handle rt_db_handle_t;
struct ControlCommand;
typedef struct ControlCommand ControlCommand;

class Turbine;

/**
 * RT_DB 数据日志记录器
 *
 * 将风电AGC场景的关键变量实时写入共享内存，
 * 支持 SCADA/HMI 模块通过共享内存监控全场运行状态，
 * 并可从指令队列接收外部调度指令实现跨进程控制。
 */
class RtDbLogger {
public:
    RtDbLogger();
    ~RtDbLogger();

    // 初始化连接共享内存，注册所有数据点
    bool initialize();

    // 清理资源
    void cleanup();

    // 每个控制周期调用：写入全场关键运行数据
    void logControlCycle(double currentTimeSec,
                         double schedulePower,
                         double totalActualPower,
                         double setpoint,
                         double errorPU,
                         double feedforwardMW,
                         double feedbackMW,
                         int cycleCount);

    // 写入各台风机状态 (前N台)
    void logTurbineStates(const std::vector<std::shared_ptr<Turbine>>& turbines,
                          const double* windSpeeds,
                          int count);

    // 仿真结束时写入最终统计数据
    void logFinalStats(double mae, double rmse, double maxError,
                       double qualifiedRate, int totalCycles);

    // 非阻塞检查调度指令队列，若有新指令返回目标功率（MW），否则返回 0
    // 返回 true 表示有新指令
    bool checkDispatchCommand(double& targetPowerMW, int& commandType);

    // 获取最大风机记录数
    int getMaxTurbinesToLog() const { return maxTurbinesToLog_; }

private:
    // 注册一个数据点，返回索引
    size_t registerPoint(const char* pointId, const char* units,
                         double initialValue, long quality);

    // 预计算所有数据点索引
    bool buildPointIndexMap();

    rt_db_handle_t* db_;          // RT_DB 连接句柄
    bool connected_;

    // 全场级数据点索引
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

    // 风机级数据点索引 (每台风机 3 个点: 功率/风速/指令)
    static const int maxTurbinesToLog_ = 100;
    size_t idxTurbinePower_[100];
    size_t idxTurbineWindSpeed_[100];
    size_t idxTurbineCommand_[100];
};
