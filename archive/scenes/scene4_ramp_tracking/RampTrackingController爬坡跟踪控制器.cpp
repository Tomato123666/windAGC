#include "RampTrackingController.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <cmath>

RampTrackingController::RampTrackingController(const RampTrackingConfig& config)
    : m_config(config) {
    m_preprocessor = std::make_unique<CommandPreprocessor>();
    m_coordinator = std::make_unique<DynamicCoordinator>();
    m_compensator = std::make_unique<FeedbackCompensator>(config.pidParams);
    m_protector = std::make_unique<ProtectionManager>(config.maxTorqueKNm, config.maxBladeMoment);
}

RampTrackingController::~RampTrackingController() = default;

void RampTrackingController::initialize() {
    m_isActive = false;
    m_currentTargetPower = 0.0f;
    m_lastError.clear();
    m_compensator->reset();
    std::cout << "[AGC] Controller initialized.\n";
}

void RampTrackingController::shutdown() {
    m_isActive = false;
    std::cout << "[AGC] Controller shutdown.\n";
}

void RampTrackingController::receiveCommand(const DispatchCommand& cmd) {
    if (!validateCommand(cmd)) {
        m_lastError = "Invalid command";
        std::cout << "[AGC] Command rejected: " << m_lastError << "\n";
        return;
    }
    startTracking(cmd);
}

void RampTrackingController::updateFarmStatus(const WindFarmStatus& status) {
    m_currentFarmStatus = status;
}

void RampTrackingController::updateTurbines(const std::vector<TurbineStatus>& turbines) {
    m_currentTurbines = turbines;
}

bool RampTrackingController::validateCommand(const DispatchCommand& cmd) {
    if (cmd.targetPowerMW < 0) return false;
    if (cmd.commandType == CommandType::RAMP && cmd.rampRateMWmin <= 0) return false;
    if (cmd.targetPowerMW > m_currentFarmStatus.totalPowerAvailable * 1.05f) return false;
    return true;
}

void RampTrackingController::startTracking(const DispatchCommand& cmd) {
    m_activeCommand = cmd;
    m_isActive = true;
    m_trackingStartTime = getCurrentTimeMs();
    m_lastStepTime = m_trackingStartTime;
    m_currentTargetPower = m_currentFarmStatus.totalPowerMW;
    m_compensator->reset();
    std::cout << "[AGC] Start tracking: target=" << cmd.targetPowerMW << " MW, type="
        << (cmd.commandType == CommandType::STEP ? "STEP" : "RAMP")
        << (cmd.commandType == CommandType::RAMP ? (" rate=" + std::to_string(cmd.rampRateMWmin) + " MW/min") : "")
        << "\n";
}

void RampTrackingController::stopTracking() {
    m_isActive = false;
    std::cout << "[AGC] Tracking finished.\n";
}

uint64_t RampTrackingController::getCurrentTimeMs() const {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

ControlResult RampTrackingController::step() {
    ControlResult result;
    if (!m_isActive) {
        result.success = true;
        result.message = "Not active";
        return result;
    }

    uint64_t now = getCurrentTimeMs();
    float dtSec = static_cast<float>(now - m_lastStepTime) / 1000.0f;
    if (dtSec <= 0.0f) dtSec = 0.001f;
    m_lastStepTime = now;

    float desiredTarget = m_activeCommand.targetPowerMW;
    if (m_activeCommand.commandType == CommandType::RAMP) {
        float elapsedSec = (now - m_trackingStartTime) / 1000.0f;
        float rampMWperSec = m_activeCommand.rampRateMWmin / 60.0f;
        float delta = rampMWperSec * elapsedSec;
        if (m_activeCommand.targetPowerMW > m_currentFarmStatus.totalPowerMW) {
            desiredTarget = m_currentFarmStatus.totalPowerMW + delta;
            if (desiredTarget > m_activeCommand.targetPowerMW)
                desiredTarget = m_activeCommand.targetPowerMW;
        }
        else {
            desiredTarget = m_currentFarmStatus.totalPowerMW - delta;
            if (desiredTarget < m_activeCommand.targetPowerMW)
                desiredTarget = m_activeCommand.targetPowerMW;
        }
    }

    float compensation = m_compensator->compute(desiredTarget, m_currentFarmStatus.totalPowerMW, dtSec);
    float compensatedTarget = desiredTarget + compensation;
    compensatedTarget = std::clamp(compensatedTarget, 0.0f, m_currentFarmStatus.totalPowerAvailable);

    std::vector<TurbineCommand> commands = m_coordinator->coordinate(
        compensatedTarget, m_currentTurbines, m_currentFarmStatus.totalPowerMW);

    if (!m_protector->isSafe(m_currentTurbines)) {
        result.success = false;
        result.message = "Protection triggered (torque or blade moment limit)";
        return result;
    }

    m_currentTargetPower = compensatedTarget;
    result.success = true;
    result.commands = commands;

    float error = std::abs(m_currentFarmStatus.totalPowerMW - m_activeCommand.targetPowerMW);
    if (error < m_config.deadbandMW) {
        stopTracking();
    }
    return result;
}
//整个AGC的核心控制器，协调预处理、功率分配、PID补偿、保护检查，执行控制步进
/*主要方法：
receiveCommand()：接收电网指令，验证后启动跟踪（startTracking）。
updateFarmStatus() / updateTurbines()：更新风场和风机数据。

step()：核心控制周期函数，执行以下步骤：

计算时间差dt。
根据指令类型（STEP/RAMP）计算期望功率（斜坡指令会按时间线性增减）。
调用PID补偿器获得补偿量，得到补偿后目标。
调用协调器将总功率分解为各风机命令。
调用保护管理器检查安全性，若不安全则返回失败。
若跟踪误差小于死区，则停止跟踪。
*/