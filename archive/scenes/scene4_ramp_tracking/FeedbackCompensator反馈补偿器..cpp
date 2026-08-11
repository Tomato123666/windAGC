#include "FeedbackCompensator.h"
#include <algorithm>

FeedbackCompensator::FeedbackCompensator(const PIDParams& params) : m_params(params) {
    reset();
}

void FeedbackCompensator::reset() {
    m_integral = 0.0f;
    m_prevError = 0.0f;
}

float FeedbackCompensator::compute(float setpoint, float measurement, float dtSec) {
    float error = setpoint - measurement;
    float proportional = m_params.Kp * error;
    m_integral += m_params.Ki * error * dtSec;
    m_integral = std::clamp(m_integral, m_params.integralMin, m_params.integralMax);
    float derivative = m_params.Kd * (error - m_prevError) / dtSec;
    m_prevError = error;
    float output = proportional + m_integral + derivative;
    return std::clamp(output, m_params.outputMin, m_params.outputMax);
}///实现PID控制器，对期望功率与实际功率的偏差进行补偿，减少稳态误差、改善动态响应。