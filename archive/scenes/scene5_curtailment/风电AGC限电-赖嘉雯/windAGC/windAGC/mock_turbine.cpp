#include "mock_turbine.h"
#include <algorithm>
#include <cmath>

// 自定义clamp函数，避免编译器对std::clamp的C++17支持问题
namespace {
    double clamp(double v, double lo, double hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }
}

MockTurbine::MockTurbine(int id)
    : id_(id),
    wind_speed_(10.0),
    health_S_(1.0),
    actual_power_(0.0),
    rng_(static_cast<unsigned>(id * 12345)),
    noise_(0.0, 0.02)
{
    telem_ = TurbineTelemetry{};
    telem_.vibration = 0.1;
    telem_.gearbox_temp = 50.0;
    telem_.gen_temp = 50.0;
    telem_.pitch_motor_temp = 45.0;
    telem_.tower_sway = 0.05;
    telem_.delay_seconds = 0.5;
    telem_.pitch_fatigue = 0.01;
    telem_.torque_fatigue = 0.01;
    telem_.run_state = 0;
}

void MockTurbine::setWind(double ws) {
    wind_speed_ = clamp(ws, 0.0, 30.0);
}

void MockTurbine::setS(double s) {
    health_S_ = clamp(s, 0.0, 1.0);
}

double MockTurbine::calcAvailablePower(double ws) const {
    if (ws < kCutInWind || ws > kCutOutWind) return 0.0;
    if (ws >= kRatedWind) return kRatedPower;
    double ratio = (ws - kCutInWind) / (kRatedWind - kCutInWind);
    return kRatedPower * ratio;
}

void MockTurbine::updateTelemetry(double dt, double power_setpoint) {
    double alpha = 1.0 - std::exp(-dt / kTau);
    actual_power_ += (power_setpoint - actual_power_) * alpha;

    double avail = calcAvailablePower(wind_speed_);
    avail *= (1.0 + noise_(rng_));
    avail = std::max(0.0, avail);
    telem_.available_power = avail;
    telem_.min_tech_power = kRatedPower * 0.10;
    telem_.actual_power = clamp(actual_power_, 0.0, avail);

    double load_ratio = (avail > 0.01) ? telem_.actual_power / avail : 0.0;
    telem_.pitch_angle = (1.0 - load_ratio) * 20.0;
    telem_.pitch_rate = 0.0;

    double load = telem_.actual_power / kRatedPower;
    double base_vib = 0.1 + load * 0.4;
    double base_gear_temp = 50.0 + load * 40.0;
    double base_gen_temp = 50.0 + load * 40.0;
    double base_pitch_temp = 45.0 + load * 20.0;
    double health_effect = 1.0 + (1.0 - health_S_) * 2.0;
    telem_.vibration = base_vib * health_effect + noise_(rng_) * 0.05;
    telem_.gearbox_temp = base_gear_temp * health_effect + noise_(rng_) * 2.0;
    telem_.gen_temp = base_gen_temp * health_effect + noise_(rng_) * 2.0;
    telem_.pitch_motor_temp = base_pitch_temp * health_effect + noise_(rng_) * 1.0;
    telem_.tower_sway = 0.05 + std::abs(noise_(rng_)) * 0.1;
    telem_.turbulence = 0.12 + std::abs(noise_(rng_)) * 0.05;
    telem_.yaw_error = noise_(rng_) * 5.0;

    telem_.pitch_fatigue = std::min(1.0, telem_.pitch_fatigue + dt * 0.0001 * load);
    telem_.torque_fatigue = std::min(1.0, telem_.torque_fatigue + dt * 0.0001 * load);

    telem_.run_state = 0;
    if (health_S_ < 0.3) telem_.run_state = 1;

    telem_.reactive_power = 0.0;
    telem_.delay_seconds = 0.5 + noise_(rng_) * 0.2;
}