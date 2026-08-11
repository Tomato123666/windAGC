/**
 * @file CommonTypes.h
 * @brief 风电场AGC统一类型定义
 *
 * 合并场景1~6的所有类型，提供统一的跨场景数据类型。
 * 各场景可从中选取所需类型，保证跨场景数据交换的一致性。
 *
 * 版本: 1.0
 * 日期: 2026-07-21
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <cmath>

// ============================================================================
// 1. 基础枚举类型
// ============================================================================

/** 调度指令类型 */
enum class CommandType : uint8_t {
    STEP = 0,       // 阶跃指令
    RAMP = 1,       // 斜坡指令
    CANCEL = 2      // 取消指令
};

/** 风机运行状态（合并场景1/4/5/6） */
enum class TurbineRunState : uint8_t {
    STOPPED = 0,            // 停机
    NORMAL = 1,             // 正常运行
    RUNNING = 1,            // 别名，兼容场景4
    SHADOW_RESTRICTED = 2,  // 阴影/尾流限制
    RESTRICTED = 3,         // 温度/振动限制
    LIMITING = 3,           // 别名，兼容场景4
    HOT_STANDBY = 4,        // 热备用
    FAULT = 5,              // 故障
    BORROWED_REG = 6        // 借用调节
};

/** 风机在AGC中的角色 */
enum class TurbineRole : uint8_t {
    BASE = 0,               // 基荷机组
    REGULATING = 1,         // 调节机组
    RESTRICTED_ROLE = 2,    // 受限角色
    SHADOW_ROLE = 3,        // 阴影受限角色
    HOT_STANDBY_ROLE = 4,   // 热备用角色
    FAULT_ROLE = 5,         // 故障角色
    BORROWED_REG_ROLE = 6   // 借用调节角色
};

/** 风电场控制模式 */
enum class FarmControlMode : uint8_t {
    STEADY_STATE = 0,       // 稳态跟踪
    RAMP_TRACKING = 1,      // 爬坡跟踪
    FREQ_RESPONSE = 2,      // 频率响应
    EMERGENCY = 3,          // 紧急模式
    SUPPRESS = 4,           // 波动抑制（场景2）
    DEEP_CURTAILMENT = 5    // 深度限电（场景5）
};

/** 一次调频状态机（场景3） */
enum class FRState : uint8_t {
    READY = 0,              // 就绪（频差在死区内）
    ACTIVE = 1,             // 调频中
    RECOVERY = 2,           // 恢复中
    FAULT = 3               // 故障
};

/** 安全子模式（场景6） */
enum class SafetySubMode : uint8_t {
    NORMAL = 0,                     // 正常
    COMM_LOSS_FREEZE = 1,           // 通信中断-冻结
    EXTREME_WIND_AUTONOMOUS = 2     // 极端风况-自治
};

/** 极端天气子类型（场景6） */
enum class ExtremeSubType : uint8_t {
    NONE = 0,
    CUT_OUT = 1,            // 切出风速
    HIGH_TURB = 2,          // 高湍流
    STORM_RIDE = 3          // 风暴穿越
};

/** 限电模式（场景5/6） */
enum class CurtailMode : uint8_t {
    MODE_A_UNIFORM = 0,     // 均匀限电
    MODE_B_PART_RUN = 1,    // 部分运行
    MODE_C_ROTOR_ENERGY = 2 // 转子储能
};

/** 风机限电状态（场景6） */
enum class FanCurtailState : uint8_t {
    FAN_NORMAL_MPPT = 0,
    FAN_CURTAIL_UNIFORM = 1,
    FAN_CURTAIL_SPEED = 2,
    FAN_STOPPED_LOCK = 3
};

// ============================================================================
// 2. 数据结构体
// ============================================================================

/** PID控制器参数 */
struct PIDParams {
    double Kp = 1.2;
    double Ki = 0.05;
    double Kd = 0.01;
    double integralMax = 10.0;
    double integralMin = -10.0;
    double outputMax = 20.0;
    double outputMin = -20.0;
};

/** 调度指令（来自电网调度中心） */
struct DispatchCommand {
    uint32_t    commandId = 0;
    uint64_t    timestamp = 0;
    double      targetPowerMW = 0.0;
    CommandType commandType = CommandType::STEP;
    double      rampRateMWmin = 5.0;
    uint32_t    validityDurationMs = 30000;
    uint8_t     priority = 1;
    uint32_t    checksum = 0;

    // 扩展字段（场景5限电用）
    bool        reserveCall = false;
    double      reserveDelta = 0.0;
};

/** 单台风机实时状态（合并各场景字段） */
struct TurbineStatus {
    // 标识
    uint16_t        turbineId = 0;
    TurbineRunState state = TurbineRunState::STOPPED;
    TurbineRole     role = TurbineRole::BASE;

    // 功率相关
    double          powerMW = 0.0;              // 当前有功功率
    double          powerSetMW = 0.0;           // 功率指令
    double          powerAvailableMax = 50.0;   // 最大可用功率
    double          powerAvailableMin = 5.0;    // 最小技术出力
    double          efficientPower = 0.0;       // 高效功率点 (90% 可用)

    // 机械/电气状态
    double          rotorSpeedRPM = 0.0;        // 转子转速
    double          pitchAngleDeg = 0.0;        // 桨距角
    double          pitchRateDegS = 0.0;        // 变桨速率
    double          torqueKNm = 0.0;            // 扭矩
    double          bladeRootMoment = 0.0;      // 叶根弯矩

    // 环境条件
    double          windSpeedMs = 0.0;          // 风速
    double          turbulence = 0.0;           // 湍流强度

    // 安全/健康（场景5）
    double          safetyIndex = 1.0;          // 安全指数 S (0~1)
    double          perfIndex = 1.0;            // 性能指数 P (0~1)
    double          maneuverIndex = 1.0;        // 可调指数 M (0~1)
    double          vibration = 0.0;            // 振动 mm/s
    double          gearboxTemp = 0.0;          // 齿轮箱温度
    double          genTemp = 0.0;              // 发电机温度
    double          pitchMotorTemp = 0.0;       // 变桨电机温度
    double          towerSway = 0.0;            // 塔筒摇摆

    // 疲劳/寿命（场景5）
    double          pitchFatigue = 0.0;         // 变桨疲劳累积
    double          torqueFatigue = 0.0;        // 扭矩疲劳累积
    double          hotStandbyHours = 0.0;      // 累计热备用时间
    uint32_t        startCount = 0;             // 启停次数

    // 效率（场景6）
    double          efficiency = 0.85;          // 效率因子
    bool            isUpwind = true;            // 是否迎风

    // 时间戳
    uint64_t        updateTime = 0;
};

/** 风电场聚合状态 */
struct FarmStatus {
    uint64_t        timestamp = 0;

    // 功率
    double          totalPowerMW = 0.0;         // 全场实际功率
    double          targetPowerMW = 0.0;        // 全场目标功率
    double          schedulePowerMW = 0.0;      // 调度计划功率
    double          totalAvailableMW = 100.0;   // 全场可用功率
    double          totalMinPowerMW = 0.0;      // 全场最小技术出力

    // 频率/电压
    double          frequencyHz = 50.0;         // 电网频率
    double          voltagePU = 1.0;            // 场站电压 (pu)

    // 风况
    double          avgWindSpeedMs = 5.0;       // 平均风速
    double          avgTurbulence = 0.0;        // 平均湍流强度

    // 模式/状态
    FarmControlMode controlMode = FarmControlMode::STEADY_STATE;
    SafetySubMode   safetyMode = SafetySubMode::NORMAL;
    ExtremeSubType  extremeType = ExtremeSubType::NONE;
    FRState         frState = FRState::READY;   // 一次调频状态
    CurtailMode     curtailMode = CurtailMode::MODE_A_UNIFORM;
    FarmControlMode opMode = FarmControlMode::STEADY_STATE;  // 运行模式(场景5兼容)

    // 通信
    bool            commHealthy = true;
    double          frozenPowerMW = 0.0;        // 通信中断冻结功率

    // 备用（场景5）
    double          reserveUpMW = 0.0;          // 上调备用
    double          reserveDownMW = 0.0;        // 下调备用
    double          curtailRatio = 0.0;         // 限电比例

    // 统计
    double          rampRateActual = 0.0;       // 实际爬坡率
    uint16_t        activeTurbineCount = 0;     // 运行中风机数

    // 性能指标
    double          MAE = 0.0;
    double          RMSE = 0.0;
    double          maxError = 0.0;
    double          qualifiedRate = 100.0;

    // 风机列表（最多100台）
    std::vector<TurbineStatus> turbines;
};

/** 单台风机控制指令 */
struct TurbineCommand {
    uint16_t    turbineId = 0;
    double      powerSetMW = 0.0;
    double      torqueSetKNm = 0.0;
    double      pitchAngleDeg = 0.0;
};

/** 控制周期结果 */
struct ControlResult {
    bool        success = false;
    std::string message;
    std::vector<TurbineCommand> commands;
};

/** 全局配置 */
struct GlobalConfig {
    // 死区/限幅
    double      deadbandMW = 0.5;
    double      deadbandPU = 0.005;
    double      maxRampRatePUPerMin = 0.10;     // 爬坡率限制 (pu/min)

    // PID默认参数
    PIDParams   pidParams;

    // 机械保护限制（场景4）
    double      maxTorqueKNm = 1500.0;
    double      maxBladeMoment = 5000.0;
    double      pitchChangeRateLimit = 5.0;     // deg/s

    // 控制周期
    uint32_t    controlCycleMs = 1000;
    double      controlPeriodSec = 1.0;

    // 前馈参数（场景1）
    double      feedforwardAlpha = 0.75;
    double      feedforwardMaxPU = 0.15;        // 15% Pn

    // 限电参数（场景5）
    double      S_warn_thr = 0.5;               // 安全指数预警阈值
    double      S_force_thr = 0.2;              // 安全指数强制保护阈值
    double      R_thresh = 0.4;                 // 限电触发阈值（限电比）
    double      T_thresh = 2.0;                 // 限电持续时间阈值 (h)
    double      ready_factor = 0.95;            // 热备用可用系数

    // 额定容量
    double      totalRatedPowerMW = 300.0;      // 全场额定功率
    double      turbineRatedMW = 3.0;           // 单台额定功率
    uint16_t    turbineCount = 100;             // 风机数量
};

/** 一次调频指令（场景3） */
struct FRCommand {
    uint16_t    turbineId = 0;
    double      basePower = 0.0;        // 基值功率
    double      frPower = 0.0;          // 调频增量
    double      finalPower = 0.0;       // 最终指令
};

// ============================================================================
// 3. 常量定义
// ============================================================================

/** 风力功率曲线标准参数 */
namespace WindConstants {
    constexpr double CUT_IN_SPEED = 3.0;        // 切入风速 (m/s)
    constexpr double RATED_SPEED = 12.0;        // 额定风速 (m/s)
    constexpr double CUT_OUT_SPEED = 25.0;      // 切出风速 (m/s)
    constexpr double AIR_DENSITY = 1.225;       // 空气密度 (kg/m³)
}

/** 电网标准参数 */
namespace GridConstants {
    constexpr double NOMINAL_FREQ = 50.0;       // 额定频率 (Hz)
    constexpr double FREQ_DEADBAND = 0.10;      // 一次调频死区 (Hz)
    constexpr double FREQ_RECOVER = 0.02;       // 恢复阈值 (Hz)
    constexpr double FREQ_READY = 0.003;        // 就绪阈值 (Hz)
    constexpr double VOLTAGE_UPPER = 1.05;      // 电压上限 (pu)
    constexpr double VOLTAGE_LOWER = 0.95;      // 电压下限 (pu)
}

/** 保护参数 */
namespace ProtectConstants {
    constexpr double MIN_STOP_SEC = 600.0;      // 最小停机时间 (s)
    constexpr double TURB_THRESHOLD = 0.25;     // 湍流保护阈值
    constexpr double MAX_PITCH_DEG = 8.0;       // 最大桨距角
    constexpr double PITCH_RATE_LIMIT = 0.3;    // 变桨速率限制 (deg/s)
}

// ============================================================================
// 4. 场景5专用：运行模式枚举（与FarmControlMode不同，场景5逻辑专用）
// ============================================================================

/** 全场运行模式（场景5限电用） */
enum class OperationMode : uint8_t {
    NORMAL_TRACKING = 0,    // 正常跟踪调度指令
    DEEP_CURTAILMENT = 1    // 深度限电模式
};

// ============================================================================
// 5. 辅助工具函数
// ============================================================================

inline const char* safetyModeStr(SafetySubMode m) {
    switch (m) {
        case SafetySubMode::NORMAL: return "正常";
        case SafetySubMode::COMM_LOSS_FREEZE: return "通信中断-冻结";
        case SafetySubMode::EXTREME_WIND_AUTONOMOUS: return "极端风况-自治";
        default: return "未知";
    }
}

inline const char* extremeTypeStr(ExtremeSubType t) {
    switch (t) {
        case ExtremeSubType::NONE: return "无";
        case ExtremeSubType::CUT_OUT: return "切出风速-紧急停机";
        case ExtremeSubType::HIGH_TURB: return "高湍流-快速降载";
        case ExtremeSubType::STORM_RIDE: return "风暴穿越-柔性降载";
        default: return "未知";
    }
}

inline const char* curtailModeStr(CurtailMode m) {
    switch (m) {
        case CurtailMode::MODE_A_UNIFORM: return "均匀限电";
        case CurtailMode::MODE_B_PART_RUN: return "部分运行";
        case CurtailMode::MODE_C_ROTOR_ENERGY: return "转子储能";
        default: return "未知";
    }
}

inline const char* frStateStr(FRState s) {
    switch (s) {
        case FRState::READY: return "就绪";
        case FRState::ACTIVE: return "调频中";
        case FRState::RECOVERY: return "已恢复";
        case FRState::FAULT: return "故障";
        default: return "未知";
    }
}

// ============================================================================
// 6. 旧式别名（向后兼容，逐步废弃）
// ============================================================================

namespace legacy {
    using WindFarmStatus = FarmStatus;
}
