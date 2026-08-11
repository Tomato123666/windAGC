#pragma once
#include <cstddef>

struct rt_db_handle;
typedef struct rt_db_handle rt_db_handle_t;

// 场景6 通信中断/极端风况安全模式 RT_DB 日志器
class RtDbLogger {
public:
    RtDbLogger();
    ~RtDbLogger();
    bool initialize();
    void cleanup();

    // 主循环日志
    void logState(int hour, int minute, float totalPower, float schedulePower,
                  float availPower, float windSpeed, float turbulence,
                  int safetyMode, int extremeType, int commStatus,
                  float frozenPower, float voltage, float curtailRatio);

    // 安全事件
    void logSafetyEvent(const char* event, float value = 0);
    // 通信事件
    void logCommEvent(bool healthy, int hour, int minute);
    // 极端天气事件
    void logExtremeEvent(int subType, float windSpeed, float turbulence);

private:
    rt_db_handle_t* db_;
    bool connected_;
    size_t idxTotalP_, idxSchedP_, idxAvailP_, idxWindS_, idxMode_;
    size_t idxExtreme_, idxComm_, idxFrozen_, idxVoltage_, idxRatio_;
};
