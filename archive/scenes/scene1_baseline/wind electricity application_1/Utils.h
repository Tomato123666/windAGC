#pragma once
#include <cmath>
#include <algorithm>

namespace Utils {

template<typename T>
T clamp(const T& value, const T& low, const T& high) {
    return (value < low) ? low : (value > high) ? high : value;
}

class LowPassFilter {
public:
    explicit LowPassFilter(double alpha = 0.3);
    double filter(double input);
    void reset();
    double filtered_;
private:
    double alpha_;
    bool initialized_;
};

class PIDController {
public:
    PIDController(double kp, double ki, double kd, double dt, double integral_limit = 0.05);
    double update(double error);
    void reset();
private:
    double kp_, ki_, kd_, dt_, integral_limit_;
    double integral_, prev_error_;
    bool output_saturated_;
    double prev_output_;
    LowPassFilter d_filter_;
};

}  // namespace Utils
