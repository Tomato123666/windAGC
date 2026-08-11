#include <iostream>
#include <vector>
#include <memory>
#include <iomanip>
#include <cstdlib>
#include <ctime>
#include <cmath>
#ifdef _WIN32
#include <windows.h>
#endif
#include "Turbine.h"
#include "AGCController.h"
#include "PredictionFusion.h"
#include "EconomicDispatcher.h"
#include "Utils.h"
#include "RtDbLogger.h"

constexpr double SIM_DURATION_SEC = 1200.0;
constexpr double AGC_PERIOD = 1.0;
constexpr double TOTAL_RATED_POWER = 300.0;
constexpr int TURBINE_COUNT = 100;

// ============================================================
// 发电计划曲线 —— 模拟调度下发的15min~24h中长期计划
// ============================================================
double scheduleFunc(double t) {
    double base = 150.0;
    double comp1 = 25.0 * std::sin(t * 0.005);
    double comp2 = 12.0 * std::sin(t * 0.0017);
    double comp3 = 8.0 * std::sin(t * 0.0032);
    return base + comp1 + comp2 + comp3;
}

// ============================================================
// 风速模型 —— 带湍流分量的真实风速模拟
// ============================================================
class WindSpeedModel {
public:
    WindSpeedModel() {
        for (int i = 0; i < TURBINE_COUNT; ++i) {
            turbulence_phase_[i] = (rand() % 10000) / 100.0;
        }
    }

    double getSpeed(double t, int turbine_id) {
        double mean = 13.0 + 1.0 * std::sin(t * 0.0008) + 0.5 * std::sin(t * 0.003);
        double turb = 0.35 * std::sin(t * 0.15 + turbulence_phase_[turbine_id])
                    + 0.20 * std::sin(t * 0.37 + turbulence_phase_[turbine_id] * 1.7)
                    + 0.12 * std::sin(t * 0.73 + turbulence_phase_[turbine_id] * 2.9);
        double speed = mean + turb;
        return Utils::clamp(speed, 3.0, 25.0);
    }

private:
    double turbulence_phase_[TURBINE_COUNT];
};

// ============================================================
// 预测函数 —— 基于超短期预测
// ============================================================
double predictionFunc(double t) {
    double true_val = scheduleFunc(t);
    double pred_error = ((rand() % 100) / 100.0 - 0.5) * 0.8;
    return true_val + pred_error;
}

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    srand(static_cast<unsigned>(time(nullptr)));
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "=== 风电AGC稳态计划跟踪仿真（集成共享内存RT_DB） ===" << std::endl;
    std::cout << "系统参数：" << std::endl;
    std::cout << "- 仿真时间：" << SIM_DURATION_SEC << "s (20min)" << std::endl;
    std::cout << "- 风电机组：" << TURBINE_COUNT << "台 x "
              << TOTAL_RATED_POWER / TURBINE_COUNT << "MW" << std::endl;
    std::cout << "- 全场额定功率：" << TOTAL_RATED_POWER << "MW" << std::endl;
    std::cout << "- 控制周期：" << AGC_PERIOD << "s" << std::endl;
    std::cout << "========================================" << std::endl << std::endl;

    // ========================
    // 初始化共享内存连接
    // ========================
    RtDbLogger rtDb;
    if (!rtDb.initialize()) {
        std::cerr << "[WARN] 共享内存连接失败，将以独立模式运行（无跨进程通信）" << std::endl;
    }

    // 风机风速缓存（用于日志记录）
    double turbineWindSpeeds[TURBINE_COUNT] = {0.0};

    // 初始化风速模型
    WindSpeedModel wind_model;

    // 初始化风机
    double init_schedule = scheduleFunc(0.0);
    double init_per_turbine = init_schedule / TURBINE_COUNT;
    std::vector<std::shared_ptr<Turbine>> turbines;
    for (int i = 0; i < TURBINE_COUNT; ++i) {
        auto t = std::make_shared<Turbine>(i, TOTAL_RATED_POWER / TURBINE_COUNT, 0.05);
        t->initializePower(init_per_turbine);
        turbines.emplace_back(t);
    }

    // 初始化前馈/调度器
    auto predictor = std::make_shared<PredictionFusion>(predictionFunc, TOTAL_RATED_POWER, 0.75, 0.15);
    auto dispatcher = std::make_shared<EconomicDispatcher>();

    // PID/前馈/爬坡率参数
    AGCController::Config agc_cfg;
    agc_cfg.control_period_sec = AGC_PERIOD;
    agc_cfg.deadband_pu = 0.003;
    agc_cfg.max_ramp_rate_pu_per_min = 0.10;
    agc_cfg.pid_kp = 1.2;
    agc_cfg.pid_ki = 0.15;
    agc_cfg.pid_kd = 0.03;
    agc_cfg.feedforward_alpha = 0.75;
    agc_cfg.feedforward_max_pu = 0.15;
    agc_cfg.total_rated_power = TOTAL_RATED_POWER;

    auto agc = std::make_shared<AGCController>(agc_cfg, predictor, dispatcher);
    agc->setScheduleFunc(scheduleFunc);

    // ========================
    // 主控制循环
    // ========================
    double current_time = 0.0;
    while (current_time < SIM_DURATION_SEC) {
        double total_actual = 0.0;
        for (auto& t : turbines) {
            double wind = wind_model.getSpeed(current_time, t->getId());
            turbineWindSpeeds[t->getId()] = wind;
            total_actual += t->runSimulation(wind, AGC_PERIOD);
        }

        double schedule_now = scheduleFunc(current_time);
        double error_mw = schedule_now - total_actual;
        double error_pu = error_mw / TOTAL_RATED_POWER;

        double setpoint = agc->runControlCycle(current_time, total_actual, turbines);

        // ── 写入共享内存 ──
        const AGCStats& stats = agc->getStats();
        rtDb.logControlCycle(current_time, schedule_now, total_actual,
                             setpoint, error_pu * 100.0,
                             0.0, 0.0,   // feedforward/feedback 由 AGCController 内部计算
                             stats.cycles);

        // 每30个周期记录一次风机详细状态（减少写入开销）
        if (stats.cycles % 30 == 0) {
            rtDb.logTurbineStates(turbines, turbineWindSpeeds, TURBINE_COUNT);
        }

        // ── 检查外部调度指令 ──
        double extTargetMW;
        int extCmdType;
        if (rtDb.checkDispatchCommand(extTargetMW, extCmdType)) {
            std::cout << "\n>>> [共享内存] 收到外部调度指令: "
                      << extTargetMW << " MW (类型=" << extCmdType << ")" << std::endl;
            // 在此可覆盖 scheduleFunc 的处理逻辑
            // 场景 1 当前以固定计划曲线为主，外部指令可用于测试
        }

        // 每100s打印状态
        if (static_cast<int>(current_time) % 100 == 0 && current_time > 0) {
            double schedule_now2 = scheduleFunc(current_time);
            std::cout << "时间: " << current_time << "s | 计划: " << schedule_now2
                << "MW | 实际: " << total_actual
                << "MW | 合格率: " << stats.qualified_rate * 100
                << "% | RMSE: " << stats.rmse * 100 << "%" << std::endl;
        }
        current_time += AGC_PERIOD;
    }

    // ========================
    // 最终统计
    // ========================
    const AGCStats& final_stats = agc->getStats();
    std::cout << "\n=== 最终统计 ===" << std::endl;
    std::cout << "控制周期数: " << final_stats.cycles << std::endl;
    std::cout << "平均误差(MAE): " << final_stats.mae * 100 << "%  [PPT目标: <=1.5%]" << std::endl;
    std::cout << "均方根误差(RMSE): " << final_stats.rmse * 100 << "%  [PPT目标: <=2%]" << std::endl;
    std::cout << "最大误差: " << final_stats.max_error * 100 << "%" << std::endl;
    std::cout << "控制合格率: " << final_stats.qualified_rate * 100 << "%  [PPT目标: >=95%]" << std::endl;

    // ── 写入最终统计到共享内存 ──
    rtDb.logFinalStats(final_stats.mae, final_stats.rmse,
                       final_stats.max_error, final_stats.qualified_rate,
                       final_stats.cycles);

    // 性能评估
    bool rmse_ok = (final_stats.rmse * 100 <= 2.0);
    bool qualified_ok = (final_stats.qualified_rate * 100 >= 95.0);
    std::cout << "\n性能评估：" << std::endl;
    std::cout << "  RMSE <= 2%: " << (rmse_ok ? "v 达标" : "x 未达标") << std::endl;
    std::cout << "  合格率 >= 95%: " << (qualified_ok ? "v 达标" : "x 未达标") << std::endl;

    // ── 共享内存自动清理（RtDbLogger 析构函数） ──
    std::cout << "\n[共享内存] 数据已持久化，其他进程可继续读取" << std::endl;

    return 0;
}
