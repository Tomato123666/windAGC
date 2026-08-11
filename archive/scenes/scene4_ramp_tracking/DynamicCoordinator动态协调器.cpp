#include "DynamicCoordinator.h"
#include <algorithm>

std::vector<TurbineCommand> DynamicCoordinator::coordinate(float targetTotalMW,
    const std::vector<TurbineStatus>& turbines,
    float currentTotalMW) {
    std::vector<TurbineCommand> commands;
    if (turbines.empty()) return commands;

    float delta = targetTotalMW - currentTotalMW;
    int nRunning = 0;
    for (const auto& t : turbines) {
        if (t.state == TurbineState::RUNNING) nRunning++;
    }
    if (nRunning == 0) return commands;

    float perTurbineDelta = delta / nRunning;

    for (const auto& t : turbines) {
        if (t.state != TurbineState::RUNNING) continue;
        TurbineCommand cmd;
        cmd.turbineId = t.turbineId;
        float newPower = t.powerMW + perTurbineDelta;
        newPower = std::clamp(newPower, t.powerAvailableMin, t.powerAvailableMax);
        cmd.powerSetMW = newPower;
        cmd.torqueSetKNm = 0.0f;
        cmd.pitchAngleDeg = 0.0f;
        commands.push_back(cmd);
    }
    return commands;
}//将风场总功率目标值动态分配到各台运行中的风机