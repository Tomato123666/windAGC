#pragma once
#include "Types.h"
#include <vector>

class DynamicCoordinator {
public:
    DynamicCoordinator() = default;
    std::vector<TurbineCommand> coordinate(float targetTotalMW,
        const std::vector<TurbineStatus>& turbines,
        float currentTotalMW);
};