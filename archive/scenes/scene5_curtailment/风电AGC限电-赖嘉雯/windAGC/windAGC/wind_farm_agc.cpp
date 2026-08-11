// ============================================================================
// wind_farm_agc.cpp
// ============================================================================
#include "wind_farm_agc.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <limits>

// ---------------------------------------------------------------------------
// 构造
// ---------------------------------------------------------------------------
WindFarmAgc::WindFarmAgc(const AgcConfig& config)
    : cfg_(config),
    farm_(),
    safety_params_(),   // 全零，调用者需通过 setSafetyParams 设置
    index_calc_(config),
    state_machine_(config),
    classifier_(config),
    mode_selector_(config),
    rest_rotation_(config)
{
    farm_.turbine_count = 0;  // 初始无机组，由外部填充
}

void WindFarmAgc::setSafetyParams(const TurbineSafetyParams& params)
{
    safety_params_ = params;
}

// ---------------------------------------------------------------------------
// 主控制循环
// ---------------------------------------------------------------------------
void WindFarmAgc::step(const AgcCommand& cmd, double dt, double T_curtail_hours)
{
    event_log_.clear();

    // ================================================================
    // 1. 安全评估与状态转移
    // ================================================================
    for (int i = 0; i < farm_.turbine_count; ++i) {
        auto& t = farm_.turbines[i];
        // 计算当前安全指数
        double S = index_calc_.computeS(t.telemetry, safety_params_);
        t.indices.S = S;

        // 状态机更新
        bool changed = false;
        TurbineState oldState = t.state;
        t.state = state_machine_.update(oldState, S, dt, changed);
        if (changed) {
            std::ostringstream oss;
            oss << "风机 " << t.id << " 状态迁移: "
                << static_cast<int>(oldState) << " -> "
                << static_cast<int>(t.state)
                << " (S=" << S << ")";
            logEvent(oss.str());
        }
    }

    // ================================================================
    // 2. 性能与机动指数计算
    // ================================================================
    for (int i = 0; i < farm_.turbine_count; ++i) {
        auto& t = farm_.turbines[i];
        t.indices.P = index_calc_.computeP(t.telemetry);
        t.indices.M = index_calc_.computeM(t.telemetry);
    }

    // ================================================================
    // 3. 全场可用功率统计（用于模式选择）
    // ================================================================
    double total_available = 0.0;
    for (int i = 0; i < farm_.turbine_count; ++i) {
        total_available += farm_.turbines[i].telemetry.available_power;
    }
    farm_.total_available = total_available;

    // 计算当前限电比例
    double target_raw = cmd.target_total_power;
    double R_curtail = 0.0;
    if (total_available > 1e-6) {
        R_curtail = (total_available - target_raw) / total_available;
        if (R_curtail < 0.0) R_curtail = 0.0;
    }

    // ================================================================
    // 4. 运行模式选择
    // ================================================================
    OperationMode newMode = mode_selector_.update(R_curtail, T_curtail_hours, farm_.mode);
    if (newMode != farm_.mode) {
        logEvent(newMode == OperationMode::DEEP_CURTAILMENT ?
            "进入深度限电模式" : "退出深度限电模式，恢复正常跟踪");
        farm_.mode = newMode;
        // 模式切换后需要重新分类
        if (newMode == OperationMode::NORMAL_TRACKING) {
            // 退出深度限电：启动所有热备用机组
            rest_rotation_.scheduleStarts(farm_, std::numeric_limits<double>::max());
        }
        // 强制触发分类器
        applyClassifier();
    }
    else {
        // 周期性分类（可根据时间累计触发，此处每个周期都执行以简化）
        // 实际工程中可结合计时器，这里每步都调用不会造成额外负担
        applyClassifier();
    }

    // 深度限电模式下的轮休调度
    if (farm_.mode == OperationMode::DEEP_CURTAILMENT) {
        // 计算运行机组高效点可吸收的下调量
        double running_absorb = 0.0;
        for (int i = 0; i < farm_.turbine_count; ++i) {
            const auto& t = farm_.turbines[i];
            if (t.state != TurbineState::HOT_STANDBY &&
                t.state != TurbineState::FAULT)
            {
                double eff_pwr = t.telemetry.available_power * 0.9;  // 与分配器一致
                running_absorb += std::max(0.0, eff_pwr - t.telemetry.min_tech_power);
            }
        }
        double stop_cap = total_available * R_curtail - running_absorb;
        if (stop_cap > 1e-3) {
            rest_rotation_.scheduleStops(farm_, stop_cap);
            logEvent("安排热备用停机，容量需求: " + std::to_string(stop_cap) + " MW");
        }
    }

    // ================================================================
    // 5. 备用容量监视
    // ================================================================
    double reserve_up = 0.0, reserve_down = 0.0;
    ReserveMonitor::compute(farm_, cfg_, reserve_up, reserve_down);
    farm_.reserve_up = reserve_up;
    farm_.reserve_down = reserve_down;

    // 备用调度（简单示例：若 reserve_call 为真，调整目标功率）
    double adjusted_target = target_raw;
    if (cmd.reserve_call) {
        adjusted_target += cmd.reserve_delta;
        logEvent("备用调度：目标调整 " + std::to_string(cmd.reserve_delta) + " MW");
    }

    // ================================================================
    // 6. 容量评估与指令校核
    // ================================================================
    double total_max = 0.0, total_min = 0.0;
    CapacityEvaluator::evaluate(farm_, total_max, total_min);
    bool capped = false;
    double target_actual = CommandChecker::checkAndClamp(adjusted_target, total_max, total_min, capped);
    if (capped) {
        logEvent("调度指令越限，钳位至 " + std::to_string(target_actual) + " MW");
    }
    farm_.total_cmd = target_actual;  // 记录最终采用的目标

    // ================================================================
    // 7. 功率分配
    // ================================================================
    Distributor::distribute(farm_, target_actual, cfg_);

    // ================================================================
    // 8. 爬坡速率限制
    // ================================================================
    RampLimiter::applyRamp(farm_, dt);

    // ================================================================
    // 9. 统计全场实际指令总和（模拟实际功率）
    // ================================================================
    double total_actual_power = 0.0;
    for (int i = 0; i < farm_.turbine_count; ++i) {
        total_actual_power += farm_.turbines[i].power_setpoint;
    }
    farm_.total_actual = total_actual_power;
}

// ---------------------------------------------------------------------------
// 内部辅助函数
// ---------------------------------------------------------------------------
void WindFarmAgc::logEvent(const std::string& msg)
{
    event_log_.push_back(msg);
}

void WindFarmAgc::applyClassifier()
{
    // 将固定数组转换为 vector 以适应 Classifier 接口
    std::vector<TurbineFullState> turbine_vec(farm_.turbine_count);
    for (int i = 0; i < farm_.turbine_count; ++i) {
        turbine_vec[i] = farm_.turbines[i];
    }

    classifier_.classify(turbine_vec, farm_.mode);

    // 写回角色字段
    for (int i = 0; i < farm_.turbine_count; ++i) {
        farm_.turbines[i].role = turbine_vec[i].role;
    }
}