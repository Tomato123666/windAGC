#pragma once
#include <memory>
#include "Utils.h"

class Turbine {
public:
    Turbine(int id, double rated_power, double min_power_ratio = 0.05);
    void setPowerCommand(double cmd);
    double runSimulation(double wind_speed, double dt_sec);
    int getId() const;
    double getRatedPower() const;
    double getMinPower() const;
    double getMaxAvailablePower() const;
    double getActualPower() const;
    bool isAvailable() const;
    void setAvailable(bool avail);
    void initializePower(double init_power);
    void reset();

private:
    int id_;
    double rated_power_;
    double min_power_;
    double cmd_power_;
    double actual_power_;
    bool available_;
    double ramp_rate_;
    Utils::LowPassFilter lpf_;
};