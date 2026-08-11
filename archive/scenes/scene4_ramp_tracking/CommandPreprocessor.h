#pragma once
#include "Types.h"

class CommandPreprocessor {
public:
    CommandPreprocessor() = default;
    bool process(const DispatchCommand& cmd, const WindFarmStatus& status,
        float& outSafeTarget, float& outSafeRampRate);
private:
    bool validateCommand(const DispatchCommand& cmd, const WindFarmStatus& status);
    float calculateSafeTarget(const DispatchCommand& cmd, const WindFarmStatus& status);
    float calculateSafeRampRate(const DispatchCommand& cmd, const WindFarmStatus& status);
};