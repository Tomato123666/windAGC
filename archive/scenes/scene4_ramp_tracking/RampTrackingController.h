#pragma once
#include "Types.h"
#include "CommandPreprocessor.h"
#include "DynamicCoordinator.h"
#include "FeedbackCompensator.h"
#include "ProtectionManager.h"
#include <memory>

class RampTrackingController {
public:
    explicit RampTrackingController(const RampTrackingConfig& config);
    ~RampTrackingController();

    void initialize();
    void shutdown();

    void receiveCommand(const DispatchCommand& cmd);
    void updateFarmStatus(const WindFarmStatus& status);
    void updateTurbines(const std::vector<TurbineStatus>& turbines);

    bool isActive() const { return m_isActive; }
    float getCurrentTargetPower() const { return m_currentTargetPower; }
    std::string getLastError() const { return m_lastError; }

    ControlResult step();

private:
    bool validateCommand(const DispatchCommand& cmd);
    void startTracking(const DispatchCommand& cmd);
    void stopTracking();
    uint64_t getCurrentTimeMs() const;

    RampTrackingConfig m_config;
    bool m_isActive = false;
    DispatchCommand m_activeCommand;
    WindFarmStatus m_currentFarmStatus;
    std::vector<TurbineStatus> m_currentTurbines;
    float m_currentTargetPower = 0.0f;
    uint64_t m_trackingStartTime = 0;
    uint64_t m_lastStepTime = 0;
    std::string m_lastError;

    std::unique_ptr<CommandPreprocessor> m_preprocessor;
    std::unique_ptr<DynamicCoordinator> m_coordinator;
    std::unique_ptr<FeedbackCompensator> m_compensator;
    std::unique_ptr<ProtectionManager> m_protector;
};