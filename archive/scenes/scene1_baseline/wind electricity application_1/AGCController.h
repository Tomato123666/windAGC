#pragma once
#include <functional>
#include <memory>
#include <vector>
#include "Utils.h"

class PredictionFusion;
class EconomicDispatcher;
class Turbine;

struct AGCStats {
    int cycles = 0;
    double mae = 0.0;
    double rmse = 0.0;
    double max_error = 0.0;
    double qualified_rate = 0.0;
};

class AGCController {
public:
    struct Config {
        double control_period_sec = 1.0;
        double deadband_pu = 0.005;
        double max_ramp_rate_pu_per_min = 0.08;
        double pid_kp = 0.8;
        double pid_ki = 0.2;
        double pid_kd = 0.02;
        double feedforward_alpha = 0.9;
        double feedforward_max_pu = 0.3;
        double total_rated_power = 300.0;
    };

    AGCController(const Config& cfg, std::shared_ptr<PredictionFusion> predictor, std::shared_ptr<EconomicDispatcher> dispatcher);
    void setScheduleFunc(std::function<double(double)> func);
    double runControlCycle(double current_time_sec, double actual_total_power, std::vector<std::shared_ptr<Turbine>>& turbines);
    const AGCStats& getStats() const;
    void resetStats();

private:
    void updateStats(double error_abs, double total_rated);
    Config cfg_;
    std::shared_ptr<PredictionFusion> predictor_;
    std::shared_ptr<EconomicDispatcher> dispatcher_;
    Utils::PIDController pid_;
    std::function<double(double)> schedule_func_;
    double prev_setpoint_;
    bool first_cycle_;       // 首次控制标志，跳过爬坡率约束
    AGCStats stats_;
};