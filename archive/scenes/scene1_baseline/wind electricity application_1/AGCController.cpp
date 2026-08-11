#include "AGCController.h"
#include "Turbine.h"
#include "PredictionFusion.h"
#include "EconomicDispatcher.h"
#include "Utils.h"
#include <cmath>

AGCController::AGCController(const Config& cfg, std::shared_ptr<PredictionFusion> predictor, std::shared_ptr<EconomicDispatcher> dispatcher)
    : cfg_(cfg), predictor_(predictor), dispatcher_(dispatcher),
    // 积分限幅提升至0.1pu（PPT要求±10%Pn），更好消除稳态误差
    pid_(cfg.pid_kp, cfg.pid_ki, cfg.pid_kd, cfg.control_period_sec, 0.10),
    prev_setpoint_(0.0), first_cycle_(true) {}

void AGCController::setScheduleFunc(std::function<double(double)> func) {
    schedule_func_ = func;
}

const AGCStats& AGCController::getStats() const {
    return stats_;
}

void AGCController::resetStats() {
    stats_ = AGCStats{};
    first_cycle_ = true;
}

double AGCController::runControlCycle(double current_time_sec, double actual_total_power, std::vector<std::shared_ptr<Turbine>>& turbines) {
    if (!schedule_func_) return prev_setpoint_;

    // 1. 获取调度计划值
    double schedule = schedule_func_(current_time_sec);

    // 2. 计算前馈量（基于超短期预测，提前30s）
    double feedforward_mw = predictor_->computeFeedforward(schedule, current_time_sec);

    // 3. 反馈PID控制
    double error_mw = schedule - actual_total_power;         // MW偏差
    double error_pu = error_mw / cfg_.total_rated_power;     // 转为标幺值
    double deadband_pu = cfg_.deadband_pu;

    // 死区处理：小偏差不触发反馈，避免频繁调节（对标PPT死区0.5%Pn）
    double error_fb_pu = (std::fabs(error_pu) > deadband_pu) ? error_pu : 0.0;
    double feedback_pu = pid_.update(error_fb_pu);
    double feedback_mw = feedback_pu * cfg_.total_rated_power;

    // 4. 合成总设定值：P_set = P_schedule + P_ff + P_fb（PPT公式）
    double total_set_mw = schedule + feedforward_mw + feedback_mw;

    // 5. 爬坡率约束（对标PPT ≤1%Pn/s，首次控制跳过以允许从初始值直接跳至计划值）
    double max_delta_mw = cfg_.max_ramp_rate_pu_per_min * (cfg_.control_period_sec / 60.0) * cfg_.total_rated_power;
    if (!first_cycle_) {
        total_set_mw = Utils::clamp(total_set_mw, prev_setpoint_ - max_delta_mw, prev_setpoint_ + max_delta_mw);
    } else {
        first_cycle_ = false;
    }

    // 6. 全场功率上下限约束
    double total_avail_mw = 0.0, total_min_mw = 0.0;
    for (const auto& t : turbines) {
        total_avail_mw += t->getMaxAvailablePower();
        total_min_mw += t->getMinPower();
    }
    total_set_mw = Utils::clamp(total_set_mw, total_min_mw, total_avail_mw);
    prev_setpoint_ = total_set_mw;

    // 7. 经济分配下发
    std::vector<double> commands = dispatcher_->dispatch(total_set_mw, turbines);
    for (size_t i = 0; i < turbines.size() && i < commands.size(); ++i) {
        turbines[i]->setPowerCommand(commands[i]);
    }

    // 8. 更新统计
    updateStats(std::fabs(error_mw), cfg_.total_rated_power);
    return total_set_mw;
}

void AGCController::updateStats(double error_abs_mw, double total_rated_mw) {
    stats_.cycles++;
    double err_pu = error_abs_mw / total_rated_mw;

    // 滑动平均 MAE
    stats_.mae = (stats_.mae * (stats_.cycles - 1) + err_pu) / stats_.cycles;
    // 滑动 RMSE
    double sq_err = err_pu * err_pu;
    stats_.rmse = std::sqrt((stats_.rmse * stats_.rmse * (stats_.cycles - 1) + sq_err) / stats_.cycles);
    // 最大误差
    stats_.max_error = std::max(stats_.max_error, err_pu);
    // 合格率：误差 < 2% Pn（PPT指标）
    bool qualified = (err_pu < 0.02);
    stats_.qualified_rate = (stats_.qualified_rate * (stats_.cycles - 1) + (qualified ? 1.0 : 0.0)) / stats_.cycles;
}
