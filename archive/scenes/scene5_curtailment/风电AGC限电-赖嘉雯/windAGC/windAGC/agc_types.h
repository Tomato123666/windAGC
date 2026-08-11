/**
 * @file agc_types.h
 * @brief 风电场自动发电控制(AGC)模块数据类型定义
 *
 * 本文件集中定义了AGC所需的所有枚举、遥测结构、安全阈值、单机/全场状态
 * 以及全局可配置参数。所有类型均为POD-like，支持默认拷贝，不依赖任何第三方库。
 * 单机数组固定容量为50，通过 turbine_count 控制实际使用数量。
 */

#ifndef AGC_TYPES_H
#define AGC_TYPES_H

#include <array>

 //==============================================================================
 // 1. 风机运行状态枚举
 //==============================================================================
enum class TurbineState {
    NORMAL,              ///< 正常发电运行
    SHADOW_RESTRICTED,   ///< 阴影限制（尾流/遮挡导致出力受限）
    RESTRICTED,          ///< 保护性限制（温度/振动等）
    HOT_STANDBY,         ///< 热备用（并网但不发电，可快速启动）
    FAULT,               ///< 故障停机
    BORROWED_REG         ///< 借调调节（参与其他场站调节）
};

//==============================================================================
// 2. 风机在AGC中的角色枚举
//==============================================================================
enum class TurbineRole {
    BASE,                ///< 基荷机组，承担固定出力
    REGULATING,          ///< 调节机组，承担功率动态调节
    RESTRICTED_ROLE,     ///< 受限机组（出力被保护性限制）
    SHADOW_ROLE,         ///< 阴影机组（尾流受限）
    HOT_STANDBY_ROLE,    ///< 热备用机组
    FAULT_ROLE,          ///< 故障机组
    BORROWED_REG_ROLE    ///< 借调调节机组
};

//==============================================================================
// 3. 全场运行模式枚举
//==============================================================================
enum class OperationMode {
    NORMAL_TRACKING,     ///< 正常跟踪调度指令
    DEEP_CURTAILMENT     ///< 深度限电模式
};

//==============================================================================
// 4. 风机遥测数据结构体
//==============================================================================
struct TurbineTelemetry {
    double vibration;           ///< 振动烈度 (mm/s)
    double gearbox_temp;        ///< 齿轮箱轴承温度 (℃)
    double gen_temp;            ///< 发电机绕组温度 (℃)
    double pitch_motor_temp;    ///< 变桨电机温度 (℃)
    double tower_sway;          ///< 塔筒摆幅 (m)
    double wind_speed;          ///< 机舱风速 (m/s)
    double turbulence;          ///< 湍流强度
    double yaw_error;           ///< 对风偏差 (°)
    double pitch_angle;         ///< 当前桨距角 (°)
    double pitch_rate;          ///< 变桨速率 (°/s)
    double available_power;     ///< 当前最大可发功率 (MW)
    double min_tech_power;      ///< 最小技术出力 (MW)
    double actual_power;        ///< 当前实际有功 (MW)
    double reactive_power;      ///< 当前无功 (Mvar)
    double delay_seconds;       ///< 响应纯滞后 (s)
    double pitch_fatigue;       ///< 变桨疲劳累积 (0~1)
    double torque_fatigue;      ///< 转矩调节疲劳累积 (0~1)
    int    run_state;           ///< 风机自身运行状态码 (0正常 1警告 2故障)
};

//==============================================================================
// 5. 安全阈值结构体
//==============================================================================
struct TurbineSafetyParams {
    double vib_trip;            ///< 振动跳机阈值
    double vib_safe;            ///< 振动安全回滞值
    double gear_temp_trip;      ///< 齿轮箱温度跳机阈值
    double gear_temp_safe;      ///< 齿轮箱温度安全回滞值
    double gen_temp_trip;       ///< 发电机温度跳机阈值
    double gen_temp_safe;       ///< 发电机温度安全回滞值
    double pitch_temp_trip;     ///< 变桨电机温度跳机阈值
    double pitch_temp_safe;     ///< 变桨电机温度安全回滞值
    double sway_trip;           ///< 塔筒摆幅跳机阈值
    double sway_safe;           ///< 塔筒摆幅安全回滞值
};

//==============================================================================
// 6. 综合指数结构体
//==============================================================================
struct TurbineIndices {
    double S;   ///< 安全指数 (0~1, 1为最安全)
    double P;   ///< 性能指数 (0~1, 1为最优性能)
    double M;   ///< 机动指数 (0~1, 1为调节能力最强)
};

//==============================================================================
// 7. 单机完整状态结构体
//==============================================================================
struct TurbineFullState {
    int                id;                  ///< 风机编号
    TurbineState       state;               ///< 当前运行状态
    TurbineRole        role;                ///< 在AGC中的角色
    TurbineTelemetry   telemetry;           ///< 实时遥测数据
    TurbineIndices     indices;             ///< 综合评估指数
    double             power_setpoint;      ///< 本次下发的有功指令 (MW)
    double             power_target_raw;    ///< 分配原始值（爬坡限制前） (MW)
    double             power_upper_limit;   ///< 当前安全/保护上限 (MW)
    double             power_lower_limit;   ///< 当前安全/保护下限 (MW)
    double             hot_standby_hours;   ///< 累计热备用时间 (h)
    int                start_count;         ///< 启停次数
};

//==============================================================================
// 8. 全场状态结构体
//==============================================================================
struct FarmState {
    double                           total_cmd;       ///< 调度下发的总目标 (MW)
    double                           total_actual;    ///< 全场实际上网功率 (MW)
    double                           total_available; ///< 全场最大可发功率总和 (MW)
    double                           reserve_up;      ///< 上调备用 (MW)
    double                           reserve_down;    ///< 下调备用 (MW)
    OperationMode                    mode;            ///< 当前运行模式
    std::array<TurbineFullState, 50> turbines;        ///< 风机状态数组（固定50台）
    int                              turbine_count;   ///< 实际参与AGC的风机数量
};

//==============================================================================
// 9. 全局可配置参数结构体（全部采用double并给出默认值）
//==============================================================================
struct AgcConfig {
    double S_warn_thr = 0.5;   ///< 安全指数预警阈值（低于此值关注）
    double S_force_thr = 0.2;   ///< 安全指数强制保护阈值（低于此值强制限制）
    double TI_penalty_coeff = 0.5;   ///< 湍流强度惩罚系数
    double w_m = 0.3;   ///< 机动指数权重
    double w_r = 0.3;   ///< 调节性能权重
    double w_pf = 0.2;   ///< 变桨疲劳权重
    double w_tf = 0.2;   ///< 转矩疲劳权重
    double w_pr = 0.1;   ///< 变桨速率权重
    double k_attenuate = 0.5;   ///< 尾流衰减系数
    double R_thresh = 0.4;   ///< 调节潜力阈值（低于此值不参与调节）
    double T_thresh = 2.0;   ///< 热备用持续时间阈值 (h)
    double C_start_equivalent_hours = 2.0; ///< 一次启停等效运行小时数 (h)
    double ready_factor = 0.95;  ///< 热备用可用系数（考虑启动成功率）
    double max_allocation_iter = 3.0;   ///< 功率分配最大迭代次数
    double reserve_confidence = 0.95;  ///< 备用容量置信度
};

#endif // AGC_TYPES_H