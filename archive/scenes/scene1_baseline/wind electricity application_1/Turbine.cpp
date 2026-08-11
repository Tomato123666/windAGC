#include "Turbine.h"
#include "Utils.h"
#include <cmath>

Turbine::Turbine(int id, double rated_power, double min_power_ratio)
    : id_(id), rated_power_(rated_power),
    min_power_(rated_power* min_power_ratio),
    cmd_power_(0.0), actual_power_(0.0), available_(true),
    // 提高爬坡率：原0.1 Pu/min → 0.15 Pu/min，对标PPT ≤1%Pn/s=0.6Pu/min
    ramp_rate_(rated_power * 0.15),
    // 提高LPF alpha：原0.4 → 0.7，加快响应速度
    lpf_(0.7) {}

void Turbine::initializePower(double init_power) {
    // 预设初始功率，消除冷启动瞬态误差
    cmd_power_ = Utils::clamp(init_power, min_power_, rated_power_);
    actual_power_ = cmd_power_;
    lpf_.reset();
    // 让LPF以当前值初始化
    lpf_.filter(actual_power_);
}

void Turbine::reset() {
    cmd_power_ = 0.0;
    actual_power_ = 0.0;
    available_ = true;
    lpf_.reset();
}

double Turbine::getMaxAvailablePower() const {
    return available_ ? rated_power_ : 0.0;
}

void Turbine::setPowerCommand(double cmd) {
    cmd_power_ = Utils::clamp(cmd, min_power_, getMaxAvailablePower());
}

double Turbine::runSimulation(double wind_speed, double dt_sec) {
    if (!available_) {
        actual_power_ = 0.0;
        return actual_power_;
    }

    // ============================================================
    // 改进的风机功率曲线 —— 实际功率曲线模型
    // v³关系在低风速区，接近额定时光滑过渡到恒功率
    // ============================================================
    double v = wind_speed;
    double max_wind_power;
    const double v_cutin = 3.0;     // 切入风速
    const double v_rated = 11.0;    // 额定风速（降低以匹配13m/s均值）
    const double v_cutout = 25.0;   // 切出风速

    if (v < v_cutin || v > v_cutout) {
        max_wind_power = 0.0;
    } else if (v < v_rated) {
        // 功率曲线：P ∝ v²（考虑到实际风机在部分负荷区的特性）
        // 比v³更平稳，更接近现代变桨风机在部分负荷区的实际表现
        double ratio = (v - v_cutin) / (v_rated - v_cutin);
        max_wind_power = rated_power_ * ratio * ratio;
    } else {
        // 额定风速以上：恒功率区
        max_wind_power = rated_power_;
    }
    max_wind_power = Utils::clamp(max_wind_power, min_power_, rated_power_);

    // 目标功率 = 指令值受限于风速可用功率
    double target = Utils::clamp(cmd_power_, min_power_, max_wind_power);

    // 爬坡率约束
    double max_delta = ramp_rate_ * (dt_sec / 60.0);
    actual_power_ = Utils::clamp(target, actual_power_ - max_delta, actual_power_ + max_delta);

    // 一阶低通滤波（模拟机电惯性，α=0.7 响应较快）
    actual_power_ = lpf_.filter(actual_power_);

    return actual_power_;
}

int Turbine::getId() const { return id_; }
double Turbine::getRatedPower() const { return rated_power_; }
double Turbine::getMinPower() const { return min_power_; }
double Turbine::getActualPower() const { return actual_power_; }
bool Turbine::isAvailable() const { return available_; }
void Turbine::setAvailable(bool avail) { available_ = avail; }
