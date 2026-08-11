#include "CommandPreprocessor.h"
#include <algorithm>

bool CommandPreprocessor::process(const DispatchCommand& cmd, const WindFarmStatus& status,
    float& outSafeTarget, float& outSafeRampRate) {
    if (!validateCommand(cmd, status)) {
        outSafeTarget = calculateSafeTarget(cmd, status);
        outSafeRampRate = calculateSafeRampRate(cmd, status);
        return false;
    }
    outSafeTarget = cmd.targetPowerMW;
    outSafeRampRate = (cmd.commandType == CommandType::RAMP) ? cmd.rampRateMWmin : status.rampRateActual;
    return true;
}

bool CommandPreprocessor::validateCommand(const DispatchCommand& cmd, const WindFarmStatus& status) {
    if (cmd.targetPowerMW < 0) return false;
    if (cmd.targetPowerMW > status.totalPowerAvailable * 1.05f) return false;
    if (cmd.commandType == CommandType::RAMP && cmd.rampRateMWmin <= 0) return false;
    return true;
}

float CommandPreprocessor::calculateSafeTarget(const DispatchCommand& cmd, const WindFarmStatus& status) {
    return std::min(cmd.targetPowerMW, status.totalPowerAvailable);
}

float CommandPreprocessor::calculateSafeRampRate(const DispatchCommand& cmd, const WindFarmStatus& status) {
    if (cmd.commandType == CommandType::RAMP && cmd.rampRateMWmin > 0)
        return cmd.rampRateMWmin;
    else
        return 5.0f;
}
//对电网下发的原始指令进行安全性和可行性预处理，输出安全的功率目标值和斜坡率。
//核心函数

//process()：主入口，调用验证函数，若验证通过则直接输出原值，否则计算安全值并返回false。

//validateCommand()：检查目标功率≥0、不超过可用功率的105% 、斜坡指令的斜坡率 > 0。

//calculateSafeTarget()：将目标功率限制在可用功率范围内（min(cmd.target, available)）。

//calculateSafeRampRate()：若为斜坡指令且斜坡率有效则返回该值，否则返回默认值5 MW / min。