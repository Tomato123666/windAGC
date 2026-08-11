#pragma once
#include <cstddef>

struct rt_db_handle;
typedef struct rt_db_handle rt_db_handle_t;

/**
 * 场景3 一次调频专用 RT_DB 日志记录器
 * 实时写入电网频率、调频功率、状态机状态到共享内存
 */
class RtDbLogger {
public:
    RtDbLogger();
    ~RtDbLogger();

    bool initialize();
    void cleanup();

    // 每个控制循环调用: 写入频率和调频数据
    void logFRCycle(double frequency, double deltaF, double df_dt,
                    int state, double totalFRPower,
                    double proportionalKW, double integralKW);

    // 写入单台风机调频指令
    void logTurbineFR(int turbineId, double basePower, double frPower,
                      double finalPower, bool running);

    // 写入扰动事件
    void logDisturbance(double triggerFreq);

private:
    rt_db_handle_t* db_;
    bool connected_;

    size_t idxFrequency_, idxFreqDelta_;
    size_t idxState_, idxTotalFR_, idxDeltaF_, idxProp_, idxInteg_;

    static const int MAX_TURB = 10;
    size_t idxTurbinePower_[10];
    size_t idxTurbineFR_[10];
    size_t idxTurbineCmd_[10];
};
