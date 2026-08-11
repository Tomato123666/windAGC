#pragma once
#include <cstddef>
#include <cstdint>

struct rt_db_handle;
typedef struct rt_db_handle rt_db_handle_t;

/**
 * 场景4 调度指令跟踪专用 RT_DB 日志记录器
 * 核心特性: 通过指令队列接收外部调度指令 (STEP/RAMP)
 * DispatchCommand ↔ ControlCommand 双向映射
 */
class RtDbLogger {
public:
    RtDbLogger();
    ~RtDbLogger();

    bool initialize();
    void cleanup();

    // 每个控制步调用: 写入跟踪状态
    void logStep(int step, float totalPowerMW, float targetPowerMW,
                 float errorMW, int controlMode, float compensation);

    // 写入单台风机状态
    void logTurbineState(int turbineId, float powerMW, float commandMW,
                         float availableMax, int state);

    // ---- 指令队列集成 ----
    // 非阻塞检查外部调度指令 (来自共享内存 ControlCommand 队列)
    // 返回 true 表示有新指令，targetMW/type 为指令参数
    bool pollDispatchCommand(float& targetMW, int& cmdType, float& rampRate, uint8_t& priority);

    // 推送当前指令状态到共享内存 (供外部监控)
    void logCommandAccepted(uint32_t cmdId, float targetMW, int cmdType);

    // 跟踪完成时调用
    void logTrackingComplete(float finalPower, float finalError);

private:
    rt_db_handle_t* db_;
    bool connected_;

    // 全场数据点索引
    size_t idxTotalPower_, idxSetpoint_, idxErrorPU_;
    size_t idxMode_, idxFeedforwardMW_, idxCycleCount_;

    // 风机索引 (最多2台)
    static const int MAX_TURB = 2;
    size_t idxPower_[2], idxCommand_[2], idxAvailable_[2];
};
