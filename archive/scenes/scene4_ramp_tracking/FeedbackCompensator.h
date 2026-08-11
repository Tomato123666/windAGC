#pragma once
#include "Types.h"

class FeedbackCompensator {
public:
    explicit FeedbackCompensator(const PIDParams& params);
    void reset();
    float compute(float setpoint, float measurement, float dtSec);
private:
    PIDParams m_params;
    float m_integral;
    float m_prevError;
};