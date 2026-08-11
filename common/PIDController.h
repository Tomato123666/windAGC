/**
 * @file PIDController.h
 * @brief 通用PID控制器（合并场景1/2/3/4/6的PID实现）
 *
 * 特性:
 * - 标准并联PID: output = Kp*e + Ki*∫e*dt + Kd*de/dt
 * - 条件积分法抗积分饱和（遇限削弱积分）
 * - 积分项限幅
 * - 微分项低通滤波（抑制高频噪声）
 * - 输出限幅
 *
 * 来源: 场景1(Utils::PIDController) + 场景2(PIDController) + 场景4(FeedbackCompensator)
 */
#pragma once

#include <algorithm>
#include <cmath>

namespace common {

/**
 * 一阶低通滤波器（内嵌用于微分滤波）
 */
class LowPassFilter {
public:
    explicit LowPassFilter(double alpha = 0.7) : alpha_(alpha) {}

    double update(double raw) {
        if (!initialized_) {
            filtered_ = raw;
            initialized_ = true;
        } else {
            filtered_ = alpha_ * raw + (1.0 - alpha_) * filtered_;
        }
        return filtered_;
    }

    void reset() { initialized_ = false; filtered_ = 0.0; }
    double value() const { return filtered_; }
    void setAlpha(double a) { alpha_ = std::clamp(a, 0.0, 1.0); }

private:
    double alpha_;
    double filtered_ = 0.0;
    bool initialized_ = false;
};

/**
 * 通用PID控制器
 */
class PIDController {
public:
    /**
     * @param kp 比例增益
     * @param ki 积分增益
     * @param kd 微分增益
     * @param dt 控制周期 (秒)
     * @param integralLimit 积分限幅 (±)
     * @param outputMin 输出下限
     * @param outputMax 输出上限
     * @param derivAlpha 微分滤波系数 (0.1=强滤波, 1.0=无滤波)
     */
    PIDController(double kp = 1.0, double ki = 0.0, double kd = 0.0,
                  double dt = 1.0,
                  double integralLimit = 10.0,
                  double outputMin = -1.0, double outputMax = 1.0,
                  double derivAlpha = 0.1)
        : kp_(kp), ki_(ki), kd_(kd), dt_(dt),
          integralLimit_(integralLimit),
          outputMin_(outputMin), outputMax_(outputMax),
          derivFilter_(derivAlpha)
    {}

    /** 执行一次PID计算 */
    double compute(double error) {
        // 1. 比例项
        double pTerm = kp_ * error;

        // 2. 积分项（条件积分抗饱和）
        // 仅在输出未饱和 或 误差方向与饱和方向相反时积分
        bool shouldIntegrate = true;
        if (saturated_) {
            // 正向饱和且误差仍为正 → 停止积分
            // 负向饱和且误差仍为负 → 停止积分
            if ((prevOutput_ >= outputMax_ - 0.01 && error > 0) ||
                (prevOutput_ <= outputMin_ + 0.01 && error < 0)) {
                shouldIntegrate = false;
            }
        }
        if (shouldIntegrate) {
            integral_ += ki_ * error * dt_;
            integral_ = std::clamp(integral_, -integralLimit_, integralLimit_);
        }
        double iTerm = integral_;

        // 3. 微分项（经低通滤波）
        double rawDeriv = (error - prevError_) / dt_;
        double derivFiltered = derivFilter_.update(rawDeriv);
        double dTerm = kd_ * derivFiltered;

        // 4. 合成输出
        double output = pTerm + iTerm + dTerm;
        output = std::clamp(output, outputMin_, outputMax_);

        // 5. 记录状态
        saturated_ = (output >= outputMax_ - 0.01) || (output <= outputMin_ + 0.01);
        prevOutput_ = output;
        prevError_ = error;

        return output;
    }

    /**
     * 扩展版compute：接受setpoint和measurement
     */
    double compute(double setpoint, double measurement) {
        return compute(setpoint - measurement);
    }

    /** 重置状态 */
    void reset() {
        integral_ = 0.0;
        prevError_ = 0.0;
        prevOutput_ = 0.0;
        saturated_ = false;
        derivFilter_.reset();
    }

    /** 获取当前积分值 */
    double getIntegral() const { return integral_; }

    /** 动态设置增益 */
    void setGains(double kp, double ki, double kd) {
        kp_ = kp; ki_ = ki; kd_ = kd;
    }

    /** 动态设置输出限幅 */
    void setOutputLimits(double min, double max) {
        outputMin_ = min; outputMax_ = max;
    }

    /** 动态设置积分限幅 */
    void setIntegralLimit(double limit) {
        integralLimit_ = limit;
    }

private:
    double kp_, ki_, kd_;
    double dt_;
    double integralLimit_;
    double outputMin_, outputMax_;

    double integral_ = 0.0;
    double prevError_ = 0.0;
    double prevOutput_ = 0.0;
    bool saturated_ = false;

    LowPassFilter derivFilter_;
};

} // namespace common
