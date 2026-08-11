#pragma once
#include <cstdint>
#include <string>

// ====== 安全子模式 (章渲祺) ======
enum class SafetySubMode : int {
    NORMAL = 0,                  // 正常模式
    COMM_LOSS_FREEZE = 1,        // 通信中断-冻结模式
    EXTREME_WIND_AUTONOMOUS = 2  // 极端风况-自治模式
};

// ====== 极端天气子类型 (章渲祺) ======
enum class ExtremeSubType : int {
    NONE = 0,
    CUT_OUT = 1,       // 切出风速 → 紧急停机
    HIGH_TURB = 2,     // 高湍流 → 快速降载
    STORM_RIDE = 3     // 风暴穿越 → 柔性降载
};

// ====== 限电模式 (李浚晞) ======
enum class CurtailMode : int {
    MODE_A_UNIFORM = 0,      // 均匀限电
    MODE_B_PART_RUN = 1,     // 部分运行+高效停机
    MODE_C_ROTOR_ENERGY = 2  // 转子储能
};

// ====== 风机状态 (李浚晞扩展) ======
enum class FanState : int {
    FAN_NORMAL_MPPT = 0,
    FAN_CURTAIL_UNIFORM = 1,
    FAN_CURTAIL_SPEED = 2,
    FAN_STOPPED_LOCK = 3
};

// ====== 单台风机 (李浚晞) ======
struct TurbineUnit {
    int id;
    float efficiency;        // 效率因子 0.7~1.0
    bool is_upwind;          // 是否迎风
    float power_max_mppt;    // MPPT最大功率 (MW)
    float power_now;         // 当前功率 (MW)
    float rotor_speed;       // 转子转速 (RPM)
    float stop_start_time;   // 停机起始时间 (sim seconds)
    FanState status;
};

// ====== 全场状态 ======
struct FarmState {
    float totalPowerMW;          // 全场实际功率
    float targetPowerMW;         // 调度目标功率
    float availablePowerMW;      // 全场可用功率
    float avgWindSpeed;          // 平均风速
    float avgTurbulence;         // 平均湍流强度
    float stationVoltage;        // 场站电压 (pu)
    float curtailRatio;          // 限电比例
    SafetySubMode safetyMode;    // 当前安全模式
    ExtremeSubType extremeType;  // 极端天气类型
    bool commHealthy;            // 通信正常
    float frozenPowerMW;         // 冻结功率
    CurtailMode curtailMode;     // 限电运行模式
};

// 常量
constexpr int MAX_TURBINES = 8;
constexpr float RATED_POWER_MW = 40.0f;     // 全场额定 40 MW
constexpr float TURBINE_RATED = 5.0f;       // 单台 5 MW
constexpr float CUT_OUT_SPEED = 25.0f;      // 切出风速
constexpr float TURB_THRESHOLD = 0.25f;     // 湍流阈值
constexpr float MIN_STOP_SEC = 600.0f;      // 最小停机时间
constexpr float VOLTAGE_UPPER = 1.05f;
constexpr float VOLTAGE_LOWER = 0.95f;

inline const char* safetyModeStr(SafetySubMode m) {
    switch (m) { case SafetySubMode::NORMAL: return "正常"; case SafetySubMode::COMM_LOSS_FREEZE: return "通信中断-冻结"; case SafetySubMode::EXTREME_WIND_AUTONOMOUS: return "极端风况-自治"; default: return "?"; }
}
inline const char* extremeTypeStr(ExtremeSubType t) {
    switch (t) { case ExtremeSubType::CUT_OUT: return "切出风速-紧急停机"; case ExtremeSubType::HIGH_TURB: return "高湍流-快速降载"; case ExtremeSubType::STORM_RIDE: return "风暴穿越-柔性降载"; default: return "无"; }
}
inline const char* curtailModeStr(CurtailMode m) {
    switch (m) { case CurtailMode::MODE_A_UNIFORM: return "均匀限电"; case CurtailMode::MODE_B_PART_RUN: return "部分运行"; case CurtailMode::MODE_C_ROTOR_ENERGY: return "转子储能"; default: return "?"; }
}
