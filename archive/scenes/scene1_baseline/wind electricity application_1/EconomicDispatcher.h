#pragma once
#include <vector>
#include <memory>

class Turbine;

class EconomicDispatcher {
public:
    EconomicDispatcher() = default;
    std::vector<double> dispatch(double total_power_target, const std::vector<std::shared_ptr<Turbine>>& turbines);
};