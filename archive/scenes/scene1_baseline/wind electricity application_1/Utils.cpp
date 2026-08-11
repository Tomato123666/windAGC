#include "Utils.h"

Utils::LowPassFilter::LowPassFilter(double alpha)
    : filtered_(0.0), alpha_(alpha), initialized_(false) {}

double Utils::LowPassFilter::filter(double input) {
    if (!initialized_) {
        filtered_ = input;
        initialized_ = true;
    } else {
        filtered_ = alpha_ * input + (1.0 - alpha_) * filtered_;
    }
    return filtered_;
}

void Utils::LowPassFilter::reset() {
    initialized_ = false;
    filtered_ = 0.0;
}

Utils::PIDController::PIDController(double kp, double ki, double kd, double dt, double integral_limit)
    : kp_(kp), ki_(ki), kd_(kd), dt_(dt), integral_limit_(integral_limit),
    integral_(0.0), prev_error_(0.0), output_saturated_(false) {}

double Utils::PIDController::update(double error) {
    // P项
    double p = kp_ * error;

    // I项 —— 遇限削弱积分法（PPT Slide 7 抗积分饱和措施）
    // 当输出已饱和且误差方向与饱和方向一致时，停止积分累积
    if (!output_saturated_ || (output_saturated_ && error * prev_output_ < 0)) {
        integral_ += error * dt_;
    }
    // 积分限幅（PPT要求 ±10%Pn）
    integral_ = clamp(integral_, -integral_limit_, integral_limit_);
    double i = ki_ * integral_;

    // D项 —— 带低通滤波的微分（抑制高频噪声）
    double raw_d = (error - prev_error_) / dt_;
    // 微分项低通滤波（减少噪声放大）
    d_filter_.filter(raw_d);
    double d = kd_ * d_filter_.filtered_;

    // 合成输出
    double output = clamp(p + i + d, -1.0, 1.0);

    // 检测输出饱和状态
    output_saturated_ = (output >= 0.99 || output <= -0.99);
    prev_output_ = output;
    prev_error_ = error;

    return output;
}

void Utils::PIDController::reset() {
    integral_ = 0.0;
    prev_error_ = 0.0;
    output_saturated_ = false;
    prev_output_ = 0.0;
    d_filter_.reset();
}
