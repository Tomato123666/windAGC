#include <iostream>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <string>
#include <functional>

enum class SafetySubMode {
    NORMAL,
    COMM_LOSS_FREEZE,
    EXTREME_WIND_AUTONOMOUS
};

enum class ExtremeSubType {
    NONE,
    CUT_OUT,
    HIGH_TURB,
    STORM_RIDE
};

// ==================== 安全模式管理器 ====================
class SafetyModeManager {
public:
    using ModeChangeCallback = std::function<void(SafetySubMode, SafetySubMode)>;

    SafetyModeManager() : current_mode_(SafetySubMode::NORMAL), frozen_power_mw_(0.0) {}
//模式决策代码
    void update(bool comm_healthy, bool extreme_weather, double current_power) {
        SafetySubMode new_mode = SafetySubMode::NORMAL;
        if (!comm_healthy) {
            new_mode = SafetySubMode::COMM_LOSS_FREEZE;
        } else if (extreme_weather) {
            new_mode = SafetySubMode::EXTREME_WIND_AUTONOMOUS;
        } else {
            new_mode = SafetySubMode::NORMAL;
        }

        if (new_mode != current_mode_) {
            SafetySubMode old = current_mode_;
            current_mode_ = new_mode;
            if (callback_) callback_(old, new_mode);
            std::cout << "[安全模式] 从 " << modeToString(old) << " 切换到 " << modeToString(new_mode) << std::endl;

            if (new_mode == SafetySubMode::COMM_LOSS_FREEZE) {
                frozen_power_mw_ = current_power;
            }
        }
    }
//模式决策代码

    SafetySubMode getCurrentMode() const { return current_mode_; }
    double getFrozenPower() const { return frozen_power_mw_; }
    void registerModeChangeCallback(ModeChangeCallback cb) { callback_ = std::move(cb); }

private:
    SafetySubMode current_mode_;
    double frozen_power_mw_;
    ModeChangeCallback callback_;

    static std::string modeToString(SafetySubMode mode) {
        switch (mode) {
            case SafetySubMode::NORMAL: return "正常模式";
            case SafetySubMode::COMM_LOSS_FREEZE: return "通信中断冻结模式";
            case SafetySubMode::EXTREME_WIND_AUTONOMOUS: return "极端风况自主模式";
            default: return "未知";
        }
    }
};

// ==================== 通信监视器 ====================
//通信看门狗代码
class CommunicationMonitor {
public:
    CommunicationMonitor() : healthy_(true) {}

    void forceSetHealthy(bool healthy) {
        if (healthy != healthy_) {
            healthy_ = healthy;
            std::cout << "[通信监视] 通信状态变为 " << (healthy_ ? "正常" : "中断") << std::endl;
        }
    }

    bool isHealthy() const { return healthy_; }

private:
    bool healthy_;
};
//通信看门狗代码

// ==================== 极端天气检测器 ====================
class ExtremeWeatherDetector {
public:
    ExtremeWeatherDetector() : extreme_(false), avg_wind_speed_(0.0), avg_turbulence_(0.0) {}

    void updateMeasurements(double avg_wind_speed, double avg_turbulence) {
        avg_wind_speed_ = avg_wind_speed;
        avg_turbulence_ = avg_turbulence;

        bool new_extreme = (avg_wind_speed_ >= cut_out_speed_) || (avg_turbulence_ >= turb_threshold_);
        if (new_extreme != extreme_) {
            extreme_ = new_extreme;
            std::cout << "[气象监测] 极端天气标志变为 " << (extreme_ ? "激活" : "解除");
            if (extreme_) {
                if (avg_wind_speed_ >= cut_out_speed_)
                    std::cout << " (原因: 超切出风速 " << avg_wind_speed_ << " m/s)";
                else if (avg_turbulence_ >= turb_threshold_)
                    std::cout << " (原因: 湍流过大 " << avg_turbulence_ << ")";
            }
            std::cout << std::endl;
        }
    }

    void forceSetExtreme(bool extreme, double wind_speed = 0.0, double turbulence = 0.0) {
        if (extreme) {
            avg_wind_speed_ = (wind_speed > 0) ? wind_speed : 26.0;
            avg_turbulence_ = (turbulence > 0) ? turbulence : 0.30;
        } else {
            avg_wind_speed_ = 12.0;
            avg_turbulence_ = 0.15;
        }
        bool new_extreme = extreme;
        if (new_extreme != extreme_) {
            extreme_ = new_extreme;
            std::cout << "[气象监测] 强制设置极端天气为 " << (extreme_ ? "激活" : "解除");
            if (extreme_) {
                if (avg_wind_speed_ >= cut_out_speed_)
                    std::cout << " (超切出风速 " << avg_wind_speed_ << " m/s)";
                else if (avg_turbulence_ >= turb_threshold_)
                    std::cout << " (湍流过大 " << avg_turbulence_ << ")";
                else
                    std::cout << " (暴风穿越模式)";
            }
            std::cout << std::endl;
        }
    }

    bool isExtreme() const { return extreme_; }
    double getAvgWindSpeed() const { return avg_wind_speed_; }
    double getAvgTurbulence() const { return avg_turbulence_; }

    ExtremeSubType getExtremeSubType() const {
        if (!extreme_) return ExtremeSubType::NONE;
        if (avg_wind_speed_ >= cut_out_speed_) return ExtremeSubType::CUT_OUT;
        if (avg_turbulence_ >= turb_threshold_) return ExtremeSubType::HIGH_TURB;
        return ExtremeSubType::STORM_RIDE;
    }

private:
    bool extreme_;
    double avg_wind_speed_;
    double avg_turbulence_;
    const double cut_out_speed_ = 25.0;
    const double turb_threshold_ = 0.25;
};

// ==================== 恢复协调器 ====================
//斜坡恢复算法
class RecoveryCoordinator {
public:
    RecoveryCoordinator() : recovering_(false), ramp_rate_mw_per_min_(2.0), start_power_(0.0), target_power_(0.0) {}

    void setParameters(double ramp_rate) { ramp_rate_mw_per_min_ = ramp_rate; }

    void startRecovery(double current_power, double target_power) {
        start_power_ = current_power;
        target_power_ = target_power;
        recovering_ = true;
        start_time_ = std::chrono::steady_clock::now();
        std::cout << "[恢复协调] 开始恢复，从 " << start_power_ << " MW 爬升至 " << target_power << " MW（速率 " << ramp_rate_mw_per_min_ << " MW/分钟）" << std::endl;
    }

    double update(double current_power, double target_schedule) {
        if (!recovering_) return target_schedule;
        auto now = std::chrono::steady_clock::now();
        double elapsed_sec = std::chrono::duration<double>(now - start_time_).count();
        double max_change = ramp_rate_mw_per_min_ * (elapsed_sec / 60.0);
        double delta = target_power_ - start_power_;
        double expected = start_power_ + (delta > 0 ? std::min(delta, max_change) : std::max(delta, -max_change));
        if (std::abs(current_power - target_schedule) <= 0.5 && elapsed_sec >= 5.0) {
            recovering_ = false;
            std::cout << "[恢复协调] 恢复完成" << std::endl;
            return target_schedule;
        }
        return expected;
    }

    bool isRecovering() const { return recovering_; }

private:
    bool recovering_;
    double ramp_rate_mw_per_min_;
    double start_power_, target_power_;
    std::chrono::steady_clock::time_point start_time_;
};
//斜坡恢复算法

// ==================== 代理 ====================
class LocalControllerProxy {
public:
    LocalControllerProxy() : last_ramp_rate_(-1.0), last_cmd_("") {}

    void setMaxRampRate(double rate) {
        if (std::abs(rate - last_ramp_rate_) > 0.01) {
            last_ramp_rate_ = rate;
            std::cout << "[代理] 最大爬坡率限制 = " << rate << " MW/分钟" << std::endl;
        }
    }

    void broadcastCommand(const std::string& cmd) {
        if (cmd != last_cmd_) {
            last_cmd_ = cmd;
            std::cout << "[代理] 向全场风机广播指令: " << cmd << std::endl;
        }
    }

private:
    double last_ramp_rate_;
    std::string last_cmd_;
};

// ==================== 主程序 ====================
int main() {
    std::srand(static_cast<unsigned>(std::time(nullptr)));
    std::cout << "=== 风电场AGC安全模式（场景6）演示 ===\n\n";

    SafetyModeManager safety_mgr;
    CommunicationMonitor comm_monitor;
    ExtremeWeatherDetector weather_detector;
    RecoveryCoordinator recovery_coord;
    LocalControllerProxy proxy;

    double rated_power = 100.0;
    recovery_coord.setParameters(2.0);

    auto get_schedule = [](int step) -> double {
        double t = step * 2.0;
        double period = 80.0;
        return 50.0 + 10.0 * sin(2.0 * M_PI * t / period);
    };

    double current_power = 50.0;
    int step_counter = 0;

    safety_mgr.registerModeChangeCallback([&](SafetySubMode old_mode, SafetySubMode new_mode) {
        if (old_mode == SafetySubMode::COMM_LOSS_FREEZE && new_mode == SafetySubMode::NORMAL) {
            if (!recovery_coord.isRecovering()) {
                double target = get_schedule(step_counter);
                recovery_coord.startRecovery(current_power, target);
            }
        }
    });

    double schedule_power = get_schedule(0);
    current_power = schedule_power;
    bool comm_healthy = true;

    double kp = 1.0, ki = 0.1, integral = 0.0;
    double dt = 2.0;

    auto extremeSubTypeToString = [](ExtremeSubType type) -> std::string {
        switch (type) {
            case ExtremeSubType::CUT_OUT: return "超切出风速-正常停机";
            case ExtremeSubType::HIGH_TURB: return "湍流过大-自主降载(15%/步)";
            case ExtremeSubType::STORM_RIDE: return "暴风穿越-剧烈降载(25%/步)";
            default: return "无";
        }
    };

    for (int step = 0; step < 50; ++step) {  // 增加步数以容纳更多阶段
        step_counter = step;
        std::this_thread::sleep_for(std::chrono::seconds(2));

        // 场景注入（调整顺序）
        if (step == 2) {
            comm_healthy = false;
            comm_monitor.forceSetHealthy(false);
            std::cout << "\n--- [场景] 通信中断 ---\n";
        }
        if (step == 7) {
            comm_healthy = true;
            comm_monitor.forceSetHealthy(true);
            std::cout << "\n--- [场景] 通信恢复 ---\n";
        }
        // 新顺序：先暴风穿越（从正常功率开始）
        if (step == 12) {
            weather_detector.forceSetExtreme(true, 23.0, 0.20); // 暴风穿越
            std::cout << "\n--- [场景] 极端天气开始（暴风穿越） ---\n";
        }
        if (step == 17) {
            weather_detector.forceSetExtreme(true, 18.0, 0.30); // 湍流过大
            std::cout << "\n--- [场景] 极端天气变化（湍流过大） ---\n";
        }
        if (step == 22) {
            weather_detector.forceSetExtreme(true, 26.0, 0.20); // 超切出风速
            std::cout << "\n--- [场景] 极端天气变化（超切出风速） ---\n";
        }
        if (step == 27) {
            weather_detector.forceSetExtreme(false);
            std::cout << "\n--- [场景] 极端天气结束 ---\n";
        }

        double new_schedule = get_schedule(step);
        if (!recovery_coord.isRecovering()) {
            schedule_power = new_schedule;
        }

        safety_mgr.update(comm_monitor.isHealthy(), weather_detector.isExtreme(), current_power);
        SafetySubMode mode = safety_mgr.getCurrentMode();

        double target_power = 0.0;
        if (mode == SafetySubMode::NORMAL) {
            if (recovery_coord.isRecovering()) {
                target_power = recovery_coord.update(current_power, schedule_power);
            } else {
                target_power = schedule_power;
            }
            proxy.setMaxRampRate(10.0);
            proxy.broadcastCommand("正常AGC指令");
        }
        else if (mode == SafetySubMode::COMM_LOSS_FREEZE) {
            target_power = safety_mgr.getFrozenPower();
            proxy.setMaxRampRate(1.0);
            proxy.broadcastCommand("冻结功率指令");
        }
        else {
            ExtremeSubType subType = weather_detector.getExtremeSubType();
            double wind_fluctuation = ((rand() % 100) - 50) / 100.0;

            if (subType == ExtremeSubType::CUT_OUT) {
                double drop = std::max(current_power * 0.3, 2.0);
                current_power -= drop;
                if (current_power < 0) current_power = 0;
                if (step == 22 || step == 23) {
                    std::cout << "[风机自主] 超切出风速，执行正常停机" << std::endl;
                }
            }
            else if (subType == ExtremeSubType::HIGH_TURB) {
                double drop = current_power * 0.15;
                current_power -= drop;
                if (current_power < 0) current_power = 0;
                if (step == 17 || step == 18) {
                    std::cout << "[风机自主] 湍流过大，执行自主降载（每步降15%）" << std::endl;
                }
            }
            else { // STORM_RIDE
                double min_power = rated_power * 0.10;  // 最低功率10MW
                // 暴风穿越：只要当前功率高于最低功率，就降25%
                if (current_power > min_power) {
                    double drop = current_power * 0.25;
                    current_power -= drop;
                    if (current_power < min_power) current_power = min_power;
                }
                // 如果已经低于最低功率，则维持（不再回升）
                if (step == 12 || step == 13) {
                    std::cout << "[风机自主] 暴风穿越模式，剧烈降载（每步降25%，最低" << min_power << "MW）" << std::endl;
                }
            }
            current_power += wind_fluctuation * 0.3;
            current_power = std::max(0.0, std::min(rated_power, current_power));
            target_power = current_power;

            proxy.setMaxRampRate(1.0);
            proxy.broadcastCommand("自主安全指令（依据风况执行对应策略）");
        }

        // 正常模式和冻结模式的功率更新
        if (mode != SafetySubMode::EXTREME_WIND_AUTONOMOUS) {
            double wind_fluctuation = ((rand() % 100) - 50) / 100.0;
            if (mode == SafetySubMode::NORMAL) {
                double error = target_power - current_power;
                integral += error * dt;
                integral = std::max(-5.0, std::min(5.0, integral));
                double output = kp * error + ki * integral;
                double max_adjust = 3.0;
                output = std::max(-max_adjust, std::min(max_adjust, output));
                current_power += output + wind_fluctuation;
                current_power = std::max(0.0, std::min(rated_power, current_power));
            }
            else if (mode == SafetySubMode::COMM_LOSS_FREEZE) {
                current_power += wind_fluctuation * 0.3;
                current_power = std::max(0.0, std::min(rated_power, current_power));
            }
        }

        std::cout << "   [目标] " << target_power << " MW" << std::endl;
        std::cout << "[第 " << step << " 步] 实际功率=" << current_power << " MW, 模式=";
        switch (mode) {
            case SafetySubMode::NORMAL: std::cout << "正常模式"; break;
            case SafetySubMode::COMM_LOSS_FREEZE: std::cout << "冻结模式"; break;
            case SafetySubMode::EXTREME_WIND_AUTONOMOUS:
                std::cout << "自主模式(" << extremeSubTypeToString(weather_detector.getExtremeSubType()) << ")";
                break;
        }
        if (mode == SafetySubMode::NORMAL && !recovery_coord.isRecovering()) {
            std::cout << " (计划值=" << schedule_power << " MW)";
        }
        std::cout << std::endl;
    }

    std::cout << "\n=== 模拟结束 ===\n";
    return 0;
}
