#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Command type
enum class CommandType : uint8_t {
    STEP,
    RAMP,
    CANCEL
};

// Turbine state
enum class TurbineState : uint8_t {
    STOPPED,
    RUNNING,
    FAULT,
    LIMITING
};

// Farm control mode
enum class FarmControlMode : uint8_t {
    STEADY_STATE,
    RAMP_TRACKING,
    FREQ_RESPONSE,
    EMERGENCY
};

// Dispatch command from grid
struct DispatchCommand {
    uint32_t commandId = 0;
    uint64_t timestamp = 0;
    float targetPowerMW = 0.0f;
    CommandType commandType = CommandType::STEP;
    float rampRateMWmin = 0.0f;
    uint32_t validityDurationMs = 30000;
    uint8_t priority = 1;
    uint32_t checksum = 0;
};

// Single turbine status
struct TurbineStatus {
    uint16_t turbineId = 0;
    TurbineState state = TurbineState::STOPPED;
    float powerMW = 0.0f;
    float powerAvailableMax = 50.0f;
    float powerAvailableMin = 5.0f;
    float rotorSpeedRPM = 0.0f;
    float pitchAngleDeg = 0.0f;
    float windSpeedMs = 0.0f;
    float torqueKNm = 0.0f;
    float bladeRootMoment = 0.0f;
    uint64_t updateTime = 0;
};

// Wind farm aggregated status
struct WindFarmStatus {
    uint64_t timestamp = 0;
    float totalPowerMW = 0.0f;
    float totalPowerAvailable = 100.0f;
    float frequencyHz = 50.0f;
    float voltagePU = 1.0f;
    uint8_t communicationStatus = 1;
    uint16_t activeTurbineCount = 0;
    float avgWindSpeedMs = 5.0f;
    FarmControlMode controlMode = FarmControlMode::STEADY_STATE;
    float rampRateActual = 0.0f;
};

// Command to a single turbine
struct TurbineCommand {
    uint16_t turbineId;
    float powerSetMW;
    float torqueSetKNm;
    float pitchAngleDeg;
};

// Result of a control step
struct ControlResult {
    bool success = false;
    std::string message;
    std::vector<TurbineCommand> commands;
};

// PID parameters
struct PIDParams {
    float Kp = 1.2f;
    float Ki = 0.05f;
    float Kd = 0.01f;
    float integralMax = 10.0f;
    float integralMin = -10.0f;
    float outputMax = 20.0f;
    float outputMin = -20.0f;
};

// Global configuration
struct RampTrackingConfig {
    float deadbandMW = 0.5f;
    float steadyStateTimeMs = 2000.0f;
    float defaultRampRateMWmin = 5.0f;
    PIDParams pidParams;
    float maxTorqueKNm = 1500.0f;
    float maxBladeMoment = 5000.0f;
    float pitchChangeRateLimit = 5.0f;
    uint32_t controlCycleMs = 1000;
};