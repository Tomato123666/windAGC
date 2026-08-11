// ============================================================================
// test_main.cpp  -  完整仿真测试主程序
// ============================================================================
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <array>
#include <cmath>

#include "agc_types.h"
#include "wind_farm_agc.h"
#include "mock_farm.h"

// 便捷的配置结构
struct SimulationConfig {
    double total_time = 300.0;
    double dt = 1.0;
};

/// 将时间对齐的脚本目标功率输出
double getTargetPower(double time) {
    if (time < 30.0) return 18.0;   // 接近满发
    if (time < 60.0) return 14.0;   // 小幅限电
    if (time < 120.0) return 8.0;   // 深度限电（需要停部分机组）
    if (time < 180.0) return 11.0;  // 备用调用（上调3MW）
    if (time < 250.0) return 8.0;
    return 18.0;                    // 恢复满发
}
/// 预测限电持续时长（小时）
double getCurtailmentDuration(double time) {
    if (time >= 60.0 && time < 250.0) return 3.0;
    return 1.0;
}

/// 是否有备用调用指令
bool isReserveCall(double time) {
    return (time >= 120.0 && time < 180.0);
}

/// 备用调用量 (MW)
double getReserveDelta() {
    return 15.0;
}

/// 格式化打印一行风机数据
void printTurbineRow(const TurbineFullState& t) {
    auto& te = t.telemetry;
    std::cout << "WT" << std::setw(2) << std::setfill('0') << t.id << std::setfill(' ')
        << "  " << std::setw(12) << (int)t.state
        << "  " << std::setw(12) << (int)t.role
        << "  " << std::fixed << std::setprecision(2)
        << std::setw(8) << te.available_power
        << "  " << std::setw(8) << te.actual_power
        << "  " << std::setw(8) << t.power_setpoint
        << "  " << std::setw(10) << t.indices.S
        << "  " << std::setw(10) << t.indices.P
        << "  " << std::setw(10) << t.indices.M
        << "  " << std::setw(6) << te.pitch_angle
        << "  " << std::setw(5) << te.vibration
        << "  " << std::setw(7) << te.gearbox_temp
        << "\n";
}

/// 将状态枚举转为简写中文
std::string stateToString(TurbineState s) {
    switch (s) {
    case TurbineState::NORMAL: return "NORMAL";
    case TurbineState::SHADOW_RESTRICTED: return "SHADOW_RES";
    case TurbineState::RESTRICTED: return "RESTRICTED";
    case TurbineState::HOT_STANDBY: return "HOT_STDBY";
    case TurbineState::FAULT: return "FAULT";
    case TurbineState::BORROWED_REG: return "BORROWED_REG";
    default: return "UNKNOWN";
    }
}
std::string roleToString(TurbineRole r) {
    switch (r) {
    case TurbineRole::BASE: return "BASE";
    case TurbineRole::REGULATING: return "REGULATING";
    case TurbineRole::RESTRICTED_ROLE: return "RESTR_ROLE";
    case TurbineRole::SHADOW_ROLE: return "SHADOW_ROLE";
    case TurbineRole::HOT_STANDBY_ROLE: return "HOT_STDBY";
    case TurbineRole::FAULT_ROLE: return "FAULT_ROLE";
    case TurbineRole::BORROWED_REG_ROLE: return "BORR_REG";
    default: return "UNKN";
    }
}

int main() {
    const int kNumTur = MockFarm::kNumTurbines;
    SimulationConfig sim;
    sim.total_time = 300.0;
    sim.dt = 1.0;

    // 创建 AGC 配置、安全参数
    AgcConfig agcConfig; // 使用默认值
    TurbineSafetyParams safety;
    safety.vib_trip = 0.8;    safety.vib_safe = 0.5;
    safety.gear_temp_trip = 95.0; safety.gear_temp_safe = 80.0;
    safety.gen_temp_trip = 95.0;  safety.gen_temp_safe = 80.0;
    safety.pitch_temp_trip = 70.0; safety.pitch_temp_safe = 60.0;
    safety.sway_trip = 0.3;   safety.sway_safe = 0.2;

    WindFarmAgc agc(agcConfig);
    agc.setSafetyParams(safety);

    MockFarm farm;
    // 初始化风机状态：全部 NORMAL, role BASE
    {
        FarmState& fs = const_cast<FarmState&>(agc.getFarmState()); // 为初始化获取可写引用
        fs.turbine_count = kNumTur;
        for (int i = 0; i < kNumTur; ++i) {
            fs.turbines[i].id = i + 1;
            fs.turbines[i].state = TurbineState::NORMAL;
            fs.turbines[i].role = TurbineRole::BASE;
            fs.turbines[i].power_setpoint = 0.0;
            fs.turbines[i].power_target_raw = 0.0;
        }
    }

    // 统计变量
    std::vector<int> roleTimeCountBase(kNumTur, 0), roleTimeCountReg(kNumTur, 0),
        roleTimeCountOther(kNumTur, 0);
    double totalAbsError = 0.0;
    int totalSteps = 0;

    std::cout << "========== 风电场AGC仿真测试开始 ==========\n";
    std::cout << "模拟风机台数: " << kNumTur << "\n";

    for (double t = 0.0; t <= sim.total_time; t += sim.dt) {
        // 准备指令
        AgcCommand cmd;
        cmd.target_total_power = getTargetPower(t);
        cmd.reserve_call = isReserveCall(t);
        cmd.reserve_delta = cmd.reserve_call ? getReserveDelta() : 0.0;
        double T_curtail_hours = getCurtailmentDuration(t);

        // 更新模拟自然环境
        farm.setWindForAll(t);

        // 模拟5号风机从 t=180s 开始 S 线性下降
        if (t >= 180.0 && t <= 210.0) {
            double frac = (t - 180.0) / 30.0; // 0->1
            double sVal = 1.0 - 0.7 * frac;   // 1.0 -> 0.3
            farm.setTurbineS(5, sVal);
        }
        else if (t > 210.0) {
            farm.setTurbineS(5, 0.3);
        }
        else {
            farm.setTurbineS(5, 1.0);
        }

        // 将物理遥测写入 FarmState
        FarmState& fs = const_cast<FarmState&>(agc.getFarmState());
        farm.fillFarmState(fs);

        // AGC 主控制步长
        agc.step(cmd, sim.dt, T_curtail_hours);

        // 获取 AGC 下发的设定值
        std::array<double, MockFarm::kNumTurbines> setpoints;
        for (int i = 0; i < kNumTur; ++i) {
            setpoints[i] = fs.turbines[i].power_setpoint;
        }
        // 将设定值驱动物理模型
        farm.applySetpoints(setpoints, sim.dt);

        // 更新 FarmState 中的 actual_power (由物理模型计算得到，但 fillFarmState 已写入 telemetry，再次同步)
        farm.fillFarmState(fs);

        // 打印当前时刻状态
        std::cout << "\n时间: " << t << " 秒\n";
        std::cout << "========== 全场状态 ==========\n";
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "调度目标: " << cmd.target_total_power << " MW | 校核后目标: "
            << fs.total_cmd << " MW | 实际上网: " << fs.total_actual
            << " MW | 模式: "
            << (fs.mode == OperationMode::NORMAL_TRACKING ? "NORMAL" : "CURTAIL") << "\n";
        std::cout << "全场可发: " << fs.total_available
            << " MW | 上调备用: " << fs.reserve_up
            << " MW | 下调备用: " << fs.reserve_down << " MW\n";

        std::cout << "---------- 机组详细 ----------\n";
        std::cout << "编号  风机状态     AGC角色     可用功率  实际功率  设定指令  安全指数S  性能指数P  机动指数M  桨距角°  振动  齿轮箱℃\n";
        for (int i = 0; i < kNumTur; ++i) {
            const auto& t = fs.turbines[i];
            std::cout << "WT" << std::setw(2) << std::setfill('0') << t.id << std::setfill(' ')
                << "  " << std::setw(12) << stateToString(t.state)
                << "  " << std::setw(12) << roleToString(t.role)
                << "  " << std::setw(8) << t.telemetry.available_power
                << "  " << std::setw(8) << t.telemetry.actual_power
                << "  " << std::setw(8) << t.power_setpoint
                << "  " << std::setw(10) << t.indices.S
                << "  " << std::setw(10) << t.indices.P
                << "  " << std::setw(10) << t.indices.M
                << "  " << std::setw(6) << t.telemetry.pitch_angle
                << "  " << std::setw(5) << t.telemetry.vibration
                << "  " << std::setw(7) << t.telemetry.gearbox_temp
                << "\n";
        }

        // 打印本步事件
        const auto& events = agc.getEventLog();
        if (!events.empty()) {
            std::cout << "---------- 事件 ----------\n";
            for (const auto& ev : events) {
                std::cout << ev << "\n";
            }
        }

        // 统计角色时间
        for (int i = 0; i < kNumTur; ++i) {
            if (fs.turbines[i].role == TurbineRole::BASE) roleTimeCountBase[i]++;
            else if (fs.turbines[i].role == TurbineRole::REGULATING) roleTimeCountReg[i]++;
            else roleTimeCountOther[i]++;
        }
        // 功率跟踪误差
        totalAbsError += std::abs(fs.total_actual - fs.total_cmd);
        totalSteps++;
    }

    // 输出统计
    std::cout << "\n\n========== 仿真统计 ==========\n";
    std::cout << "各机组角色时间占比:\n";
    for (int i = 0; i < kNumTur; ++i) {
        std::cout << "WT" << std::setw(2) << std::setfill('0') << (i + 1) << std::setfill(' ')
            << ": BASE " << std::setw(4) << roleTimeCountBase[i] << " 步, "
            << "REGULATING " << std::setw(4) << roleTimeCountReg[i] << " 步, "
            << "其他 " << roleTimeCountOther[i] << " 步\n";
    }

    double mae = totalAbsError / totalSteps;
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "功率跟踪平均绝对误差: " << mae << " MW\n";

    std::cout << "仿真结束。\n";
    return 0;
}