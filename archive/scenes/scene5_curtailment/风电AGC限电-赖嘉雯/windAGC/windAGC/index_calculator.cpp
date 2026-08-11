// ============================================================================
// index_calculator.cpp
// ============================================================================
#include "index_calculator.h"
#include <algorithm>
#include <cmath>

namespace {

    // -------------------- 物理常量 --------------------
    constexpr double PI = 3.14159265358979323846;
    constexpr double kRatedPower = 2.0;       ///< 单机额定功率 (MW)，可根据机组型号调整
    constexpr double kCutInWindSpeed = 3.0;       ///< 切入风速 (m/s)
    constexpr double kRatedWindSpeed = 12.0;      ///< 额定风速 (m/s)
    constexpr double kCutOutWindSpeed = 25.0;      ///< 切出风速 (m/s)
    constexpr double kTurbulenceRef = 0.2;       ///< 湍流强度参考值
    constexpr double kMaxDelay = 5.0;       ///< 最大响应纯滞后 (s)
    constexpr double kMaxPitchRate = 10.0;      ///< 最大变桨速率 (°/s)

    /**
     * @brief 数值钳位
     * @param val 输入值
     * @param lo  下界
     * @param hi  上界
     * @return 限制在 [lo, hi] 内的值
     */
    inline double clamp(double val, double lo, double hi) {
        return std::max(lo, std::min(val, hi));
    }

}   // anonymous namespace

// ---------------------------------------------------------------------------
// 构造 / 析构
// ---------------------------------------------------------------------------
IndexCalculator::IndexCalculator(const AgcConfig& config)
    : config_(config)
{
}

// ---------------------------------------------------------------------------
// 安全指数 S
// ---------------------------------------------------------------------------
double IndexCalculator::computeS(const TurbineTelemetry& telem,
    const TurbineSafetyParams& safety) const
{
    // 任一遥测值达到或超过跳机阈值 → 直接判为极度危险
    if (telem.vibration >= safety.vib_trip ||
        telem.gearbox_temp >= safety.gear_temp_trip ||
        telem.gen_temp >= safety.gen_temp_trip ||
        telem.pitch_motor_temp >= safety.pitch_temp_trip ||
        telem.tower_sway >= safety.sway_trip)
    {
        return 0.0;
    }

    /**
     * @brief 计算单维度健康度
     * @param actual 实际值
     * @param trip   跳机阈值
     * @param safe   安全回滞值
     * @return 健康度 (1.0 完全正常 → 0.0 到达跳机边界)
     */
    auto calcHealth = [](double actual, double trip, double safe) -> double {
        if (trip <= safe) {
            // 阈值配置不合理（分母≤0），此时只要未跳机即视为健康
            return 1.0;
        }
        double norm = (actual - safe) / (trip - safe);
        norm = clamp(norm, 0.0, 1.0);
        return 1.0 - norm;
        };

    double hVib = calcHealth(telem.vibration, safety.vib_trip, safety.vib_safe);
    double hGear = calcHealth(telem.gearbox_temp, safety.gear_temp_trip, safety.gear_temp_safe);
    double hGen = calcHealth(telem.gen_temp, safety.gen_temp_trip, safety.gen_temp_safe);
    double hPitch = calcHealth(telem.pitch_motor_temp, safety.pitch_temp_trip, safety.pitch_temp_safe);
    double hSway = calcHealth(telem.tower_sway, safety.sway_trip, safety.sway_safe);

    // 安全指数取最差的健康度（木桶原理）
    return std::min({ hVib, hGear, hGen, hPitch, hSway });
}

// ---------------------------------------------------------------------------
// 理想功率查表
// ---------------------------------------------------------------------------
double IndexCalculator::powerCurveLookup(double windSpeed) const
{
    if (windSpeed < kCutInWindSpeed || windSpeed > kCutOutWindSpeed) {
        return 0.0;
    }
    if (windSpeed >= kRatedWindSpeed) {
        return kRatedPower;
    }
    // 线性插值 (切入 → 0，额定风速 → 额定功率)
    double ratio = (windSpeed - kCutInWindSpeed) /
        (kRatedWindSpeed - kCutInWindSpeed);
    return kRatedPower * ratio;
}

// ---------------------------------------------------------------------------
// 性能指数 P
// ---------------------------------------------------------------------------
double IndexCalculator::computeP(const TurbineTelemetry& telem) const
{
    // 1) 理想功率系数
    double idealPower = powerCurveLookup(telem.wind_speed);
    double P_ideal = idealPower / kRatedPower;

    // 2) 湍流惩罚
    double turbRatio = std::min(telem.turbulence / kTurbulenceRef, 1.0);
    double TI_penalty = 1.0 - turbRatio * config_.TI_penalty_coeff;

    // 3) 对风偏差惩罚（余弦衰减）
    double yawRad = telem.yaw_error * PI / 180.0;
    double yawPenalty = std::cos(yawRad);

    double P = P_ideal * TI_penalty * yawPenalty;
    return std::max(0.0, P);
}

// ---------------------------------------------------------------------------
// 机动指数 M
// ---------------------------------------------------------------------------
double IndexCalculator::computeM(const TurbineTelemetry& telem) const
{
    // 上调容量、下调容量，取较小者作为可调度裕度
    double upCap = telem.available_power - telem.min_tech_power;
    double downCap = kRatedPower - telem.actual_power;
    double marginFactor = std::min(upCap, downCap) / kRatedPower;
    marginFactor = clamp(marginFactor, 0.0, 1.0);

    // 响应速度因子
    double delayRatio = clamp(telem.delay_seconds / kMaxDelay, 0.0, 1.0);
    double respFactor = 1.0 - delayRatio;

    // 变桨速率惩罚
    double pitchRatio = clamp(std::abs(telem.pitch_rate) / kMaxPitchRate, 0.0, 1.0);

    // 加权综合
    double M = config_.w_m * marginFactor
        + config_.w_r * respFactor
        + config_.w_pf * (1.0 - telem.pitch_fatigue)
        + config_.w_tf * (1.0 - telem.torque_fatigue)
        - config_.w_pr * pitchRatio;

    return clamp(M, 0.0, 1.0);
}