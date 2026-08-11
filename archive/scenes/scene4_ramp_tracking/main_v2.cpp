/**
 * @file main_v2.cpp
 * @brief 场景4 调度指令阶跃斜坡变化 —— 统合架构入口 (V2)
 *
 * 变更点:
 *   1. 使用 RtDbLoggerV2 替代原 RtDbLogger
 *   2. 引入 ../common/CommonTypes.h 统一类型体系
 *   3. 所有控制算法逻辑与原 main.cpp 保持一致
 */
#include "RampTrackingController.h"
#include "Types.h"
#include "RtDbLoggerV2.h"
#include "../common/CommonTypes.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <iomanip>
#include <vector>
#include <cmath>
#ifdef _WIN32
#include <windows.h>
#endif

// 风机模拟器
class TurbineSimulator {
public:
    static void applyCommands(std::vector<TurbineStatus>& turbines,
        const std::vector<TurbineCommand>& commands, float dtSec) {
        for (const auto& cmd : commands) {
            for (auto& t : turbines) {
                if (t.turbineId == cmd.turbineId && t.state == TurbineState::RUNNING) {
                    float diff = cmd.powerSetMW - t.powerMW;
                    float step = diff * dtSec * 0.5f;
                    t.powerMW += step;
                    if (std::abs(t.powerMW - cmd.powerSetMW) < 0.01f)
                        t.powerMW = cmd.powerSetMW;
                    t.torqueKNm = (t.powerMW / t.powerAvailableMax) * 1000.0f;
                    break;
                }
            }
        }
    }
};

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    // ── 初始化共享内存（使用 V2 日志器） ──
    RtDbLoggerV2 rtDb;
    bool hasShm = rtDb.initialize();
    if (hasShm) {
        std::cout << "[共享内存] 已连接 | 指令队列: 可接收外部STEP/RAMP指令\n";
    }

    std::cout << "=== Wind AGC Ramp Tracking Simulation (统合架构 V2) ===\n";

    RampTrackingConfig config;
    config.deadbandMW = 0.5f;
    config.controlCycleMs = 1000;
    config.pidParams.Kp = 0.3f;
    config.pidParams.Ki = 0.0f;
    config.pidParams.Kd = 0.0f;
    config.pidParams.outputMax = 5.0f;
    config.pidParams.outputMin = -5.0f;

    RampTrackingController controller(config);
    controller.initialize();

    // 初始化风机：两台各 15 MW
    std::vector<TurbineStatus> turbines(2);
    turbines[0] = { 1, TurbineState::RUNNING, 15.0f, 50.0f, 5.0f, 1200.0f, 10.0f, 6.0f, 300.0f, 0 };
    turbines[1] = { 2, TurbineState::RUNNING, 15.0f, 50.0f, 5.0f, 1200.0f, 10.0f, 6.0f, 300.0f, 0 };
    WindFarmStatus farm;
    farm.totalPowerMW = 30.0f;
    farm.totalPowerAvailable = 100.0f;
    farm.activeTurbineCount = 2;
    farm.controlMode = FarmControlMode::STEADY_STATE;

    controller.updateFarmStatus(farm);
    controller.updateTurbines(turbines);

    // ── 尝试从共享内存指令队列获取外部指令 ──
    float extTargetMW;
    int   extCmdType;
    float extRampRate;
    uint8_t extPriority;
    bool hasExternalCmd = rtDb.pollDispatchCommand(extTargetMW, extCmdType,
                                                    extRampRate, extPriority);

    if (hasExternalCmd) {
        // 外部指令优先
        DispatchCommand extCmd;
        extCmd.commandId = 0;
        extCmd.targetPowerMW = extTargetMW;
        extCmd.commandType = (extCmdType == 1) ? CommandType::STEP : CommandType::RAMP;
        extCmd.rampRateMWmin = extRampRate;
        extCmd.priority = extPriority;
        std::cout << "\n>>> [共享内存] 收到外部调度: " << extTargetMW
                  << " MW (" << (extCmd.commandType == CommandType::STEP ? "STEP" : "RAMP") << ")\n";
        controller.receiveCommand(extCmd);
        rtDb.logCommandAccepted(extCmd.commandId, extTargetMW, extCmdType);
    } else {
        // 使用内置测试指令
        DispatchCommand cmd;
        cmd.commandId = 1;
        cmd.targetPowerMW = 70.0f;
        cmd.commandType = CommandType::STEP;
        cmd.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

        std::cout << "\n>>> [内置] 发送 STEP 指令: target = " << cmd.targetPowerMW << " MW\n";
        controller.receiveCommand(cmd);
        rtDb.logCommandAccepted(cmd.commandId, cmd.targetPowerMW, 0);
    }

    // ── 控制循环 ──
    int step = 0;
    const int MAX_STEPS = 60;
    const float DT_SEC = 0.1f;

    while (step < MAX_STEPS && controller.isActive()) {
        // 每10步检查一次外部指令（不阻塞主循环）
        if (step > 0 && step % 10 == 0) {
            if (rtDb.pollDispatchCommand(extTargetMW, extCmdType,
                                          extRampRate, extPriority)) {
                DispatchCommand newCmd;
                newCmd.commandId = step;
                newCmd.targetPowerMW = extTargetMW;
                newCmd.commandType = (extCmdType == 1) ? CommandType::STEP : CommandType::RAMP;
                newCmd.rampRateMWmin = extRampRate;
                newCmd.priority = extPriority;
                controller.receiveCommand(newCmd);
                std::cout << ">>> 指令已更新!\n";
            }
        }

        ControlResult res = controller.step();
        if (!res.success) {
            std::cout << "Control error: " << res.message << "\n";
            break;
        }

        TurbineSimulator::applyCommands(turbines, res.commands, DT_SEC);

        float total = 0.0f;
        for (auto& t : turbines) total += t.powerMW;
        farm.totalPowerMW = total;
        controller.updateFarmStatus(farm);
        controller.updateTurbines(turbines);

        // ── 写入共享内存（V2 日志器） ──
        if (hasShm) {
            float errorMW = total - controller.getCurrentTargetPower();
            rtDb.logStep(step, total, controller.getCurrentTargetPower(),
                         errorMW, (int)farm.controlMode, 0.0f);

            // 写入爬坡状态（V2 新增 RAMP.* 数据点）
            rtDb.logRampState(
                controller.getCurrentTargetPower(),          // currentTarget
                extCmdType != 0 ? extTargetMW : 70.0f,       // desiredTarget
                controller.getCurrentTargetPower(),          // compensatedTarget (same for now)
                controller.isActive(),                       // isActive
                config.defaultRampRateMWmin,                 // rampRate
                true                                         // protectionSafe
            );

            // 记录各台风机
            for (int i = 0; i < 2; i++) {
                rtDb.logTurbineState(i, turbines[i].powerMW,
                    (i < (int)res.commands.size() ? res.commands[i].powerSetMW : 0.0f),
                    turbines[i].powerAvailableMax, (int)turbines[i].state);
            }
        }

        std::cout << std::fixed << std::setprecision(2);
        std::cout << "Step " << std::setw(2) << step
            << " | Power = " << std::setw(6) << farm.totalPowerMW << " MW"
            << " | Target = " << std::setw(6) << controller.getCurrentTargetPower() << " MW"
            << " | Error = " << std::setw(6) << (farm.totalPowerMW - controller.getCurrentTargetPower())
            << " MW\n";

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        step++;
    }

    // ── 最终结果 ──
    float finalError = farm.totalPowerMW - controller.getCurrentTargetPower();
    if (hasShm) rtDb.logTrackingComplete(farm.totalPowerMW, finalError);

    std::cout << "\n=== Final ===\n";
    std::cout << "Total Power: " << farm.totalPowerMW << " MW\n";
    std::cout << "Target: " << controller.getCurrentTargetPower() << " MW\n";
    std::cout << "Final Error: " << finalError << " MW\n";
    std::cout << "Active: " << (controller.isActive() ? "YES" : "NO") << "\n";
    if (hasShm) std::cout << "[共享内存] 跟踪数据已持久化\n";

    controller.shutdown();
    return 0;
}
