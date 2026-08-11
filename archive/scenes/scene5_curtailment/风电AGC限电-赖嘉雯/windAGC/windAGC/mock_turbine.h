#ifndef MOCK_TURBINE_H
#define MOCK_TURBINE_H

#include <random>
#include "agc_types.h"

class MockTurbine {
public:
    static constexpr double kRatedPower = 2.0;
    static constexpr double kCutInWind = 3.0;
    static constexpr double kRatedWind = 12.0;
    static constexpr double kCutOutWind = 25.0;
    static constexpr double kTau = 2.0;

    // 添加默认构造函数，以便 std::array 能够默认初始化
    MockTurbine() : id_(0), wind_speed_(0.0), health_S_(0.0), actual_power_(0.0),
        rng_(0), noise_(0.0, 0.02) {
    }

    explicit MockTurbine(int id);

    void setWind(double ws);
    void setS(double s);

    TurbineTelemetry getTelemetry() const { return telem_; }
    void updateTelemetry(double dt, double power_setpoint);

private:
    int id_;
    double wind_speed_ = 10.0;
    double health_S_ = 1.0;
    double actual_power_ = 0.0;

    TurbineTelemetry telem_;

    std::mt19937 rng_;
    std::normal_distribution<double> noise_;

    double calcAvailablePower(double ws) const;
};

#endif