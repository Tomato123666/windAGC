#pragma once
#include "Types.h"
#include <vector>

class ProtectionManager {
public:
    explicit ProtectionManager(float maxTorqueKNm, float maxBladeMoment);
    bool isSafe(const std::vector<TurbineStatus>& turbines) const;
    void setLimits(float maxTorque, float maxBladeMoment);
private:
    float m_maxTorqueKNm;
    float m_maxBladeMoment;
};