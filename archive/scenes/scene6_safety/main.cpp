/**
 * 风电AGC场景6: 通信中断或极端风况安全模式
 * 整合 章渲祺(安全状态机+恢复协调) + 李浚晞(24h调度+AVC+群控)
 * 集成共享内存 RT_DB
 */
#include <iostream>
#include <iomanip>
#include <cmath>
#include <cstdlib>
#include <ctime>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include "types.h"
#include "safety_manager.h"
#include "comm_monitor.h"
#include "weather_detector.h"
#include "recovery_coord.h"
#include "fan_control.h"
#include "dispatch_sim.h"
#include "RtDbLogger.h"

TurbineUnit g_turbines[MAX_TURBINES]; // 全局风机阵列

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    std::srand((unsigned)std::time(nullptr));

    // ── 共享内存 ──
    RtDbLogger rtDb;
    bool hasShm = rtDb.initialize();
    if (hasShm) std::cout << "[共享内存] 已连接" << std::endl;

    // ── 初始化各模块 ──
    fan_init(g_turbines, MAX_TURBINES);
    SafetyModeManager safetyMgr;
    CommunicationMonitor commMon;
    ExtremeWeatherDetector weatherDet;
    RecoveryCoordinator recovery;
    recovery.setRampRate(2.0f);  // 2MW/min 恢复爬坡

    float currentPower = 0.0f;
    float simTimeSec = 0.0f;
    const float kDtMin = 1.0f;   // 1分钟步长
    int totalSteps = 24 * 60;    // 1440 分钟 = 24小时
    CurtailMode lastCurtailMode = CurtailMode::MODE_A_UNIFORM;
    SafetySubMode lastSafetyMode = SafetySubMode::NORMAL;

    // 模式切换回调：通信恢复时启动斜坡恢复
    safetyMgr.onModeChange([&](SafetySubMode oldM, SafetySubMode newM) {
        if (oldM == SafetySubMode::COMM_LOSS_FREEZE && newM == SafetySubMode::NORMAL) {
            if (!recovery.isRecovering()) {
                int h = (int)(simTimeSec / 3600.0f);
                recovery.start(currentPower, get_schedule_power(h));
            }
        }
    });

    // 初始功率
    for (int i = 0; i < MAX_TURBINES; i++)
        currentPower += g_turbines[i].power_now;

    std::cout << "╔══════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  风电AGC场景6: 通信中断/极端风况安全模式         ║" << std::endl;
    std::cout << "║  整合版 (安全状态机 + 24h调度 + AVC + 群控)      ║" << std::endl;
    std::cout << "║  机组: " << MAX_TURBINES << "×" << TURBINE_RATED
              << "MW | 全场: " << RATED_POWER_MW << "MW              ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════╝" << std::endl;

    std::cout << std::fixed << std::setprecision(2);

    // ── 24小时逐分钟主循环 ──
    for (int step = 0; step < totalSteps; step++) {
        simTimeSec = step * 60.0f;
        int hour   = step / 60;
        int minute = step % 60;

        // 1. 调度计划
        float windFactor = hour_wind_factor[hour];
        float availPower = RATED_POWER_MW * windFactor;
        float schedulePwr = get_schedule_power(hour);

        CurtailMode cMode = get_mode_by_hour(hour);
        float cRatio = get_curtail_ratio(hour);
        float voltage = get_station_voltage(hour);

        // 2. 通信状态 (时段模拟，无随机抖动)
        bool commHealthy = simulate_comm_status(hour, minute);
        commMon.setHealthy(commHealthy);

        // 3. 极端天气检测
        float windSpeed = 10.0f + 2.0f * windFactor
                        + 0.5f * std::sin(simTimeSec * 0.001f);
        float turbulence = 0.10f + 0.05f * std::sin(simTimeSec * 0.003f);

        bool hasExtreme = simulate_extreme_wind(hour, minute, windSpeed, turbulence);
        if (hasExtreme && step % 30 == 0)
            weatherDet.forceSet(true, windSpeed, turbulence);
        else if (!hasExtreme)
            weatherDet.forceSet(false);

        // 4. 安全模式决策
        safetyMgr.update(commMon.isHealthy(), weatherDet.isExtreme(), currentPower);
        SafetySubMode safeMode = safetyMgr.getMode();

        // 5. 功率计算
        float targetPower = schedulePwr;
        if (safeMode == SafetySubMode::COMM_LOSS_FREEZE) {
            targetPower = safetyMgr.getFrozenPower();
        } else if (safeMode == SafetySubMode::EXTREME_WIND_AUTONOMOUS) {
            ExtremeSubType sub = weatherDet.subType();
            targetPower = extreme_power_cut(sub, currentPower, RATED_POWER_MW, turbulence);
        } else if (safeMode == SafetySubMode::NORMAL) {
            if (recovery.isRecovering()) {
                recovery.update(kDtMin * 60.0f); // dt in seconds
                targetPower = recovery.getTarget(currentPower, schedulePwr);
            }
        }

        // 6. 执行风机控制 (仅模式变化时执行 + 非B模式时检查重启)
        bool modeChanged = (cMode != lastCurtailMode || safeMode != lastSafetyMode);
        if (modeChanged) {
            fan_execute_mode(g_turbines, MAX_TURBINES, cMode, cRatio);
            lastCurtailMode = cMode;
            lastSafetyMode = safeMode;
        }
        // 仅在非B模式时检查风机重启
        if (cMode != CurtailMode::MODE_B_PART_RUN)
            fan_restart_check(g_turbines, MAX_TURBINES, simTimeSec);

        // 进入非正常模式时取消恢复
        if (safeMode != SafetySubMode::NORMAL && recovery.isRecovering()) {
            recovery.update(0);  // 强制重置 (下一周期取消防恢复)
        }

        // 7. 功率调节 (PI + 随机噪声模拟)
        float err = targetPower - currentPower;
        float adjust = 0.3f * err + ((std::rand() % 100) - 50) * 0.02f;
        adjust = std::max(-3.0f, std::min(3.0f, adjust));
        currentPower += adjust;
        currentPower = std::max(0.0f, std::min(RATED_POWER_MW, currentPower));

        // 8. AVC 保护
        protect_avc_check(voltage);

        // 9. 写共享内存
        if (hasShm && step % 2 == 0) { // 每2分钟写一次
            rtDb.logState(hour, minute, currentPower, schedulePwr,
                          availPower, windSpeed, turbulence,
                          (int)safeMode, (int)weatherDet.subType(),
                          commHealthy ? 1 : 0,
                          safetyMgr.getFrozenPower(), voltage, cRatio);
        }

        // 10. 控制台输出 (每小时或事件时)
        if (minute == 0 || (step > 0 && step % 120 == 0)) {
            std::cout << "[" << std::setw(2) << std::setfill('0') << hour
                      << ":" << std::setw(2) << minute << "] "
                      << "P=" << std::setw(5) << currentPower << "MW "
                      << "Tgt=" << std::setw(5) << targetPower << "MW "
                      << "风=" << std::setw(4) << windSpeed << "m/s "
                      << "模式=" << safetyModeStr(safeMode);
            if (safeMode == SafetySubMode::EXTREME_WIND_AUTONOMOUS)
                std::cout << "(" << extremeTypeStr(weatherDet.subType()) << ")";
            if (!commHealthy) std::cout << " [通信中断]";
            if (recovery.isRecovering()) std::cout << " [恢复中]";
            std::cout << std::endl;
            std::cout << std::setfill(' ');
        }

        // 事件日志
        if (hasShm && !commHealthy && minute == 0)
            rtDb.logCommEvent(false, hour, minute);
        if (hasShm && hasExtreme && minute == 0)
            rtDb.logExtremeEvent((int)weatherDet.subType(), windSpeed, turbulence);
    }

    // ── 24h 总结 ──
    std::cout << "\n=== 24小时仿真完成 ===" << std::endl;
    std::cout << "最终功率: " << currentPower << " MW" << std::endl;
    if (hasShm) std::cout << "[共享内存] 安全模式数据已持久化" << std::endl;

    return 0;
}
