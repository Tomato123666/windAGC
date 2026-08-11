#include "ProtectionManager.h"

ProtectionManager::ProtectionManager(float maxTorqueKNm, float maxBladeMoment)
    : m_maxTorqueKNm(maxTorqueKNm), m_maxBladeMoment(maxBladeMoment) {
}

bool ProtectionManager::isSafe(const std::vector<TurbineStatus>& turbines) const {
    for (const auto& t : turbines) {
        if (t.torqueKNm > m_maxTorqueKNm * 1.1f) return false;
        if (t.bladeRootMoment > m_maxBladeMoment * 1.1f) return false;
    }
    return true;
}

void ProtectionManager::setLimits(float maxTorque, float maxBladeMoment) {
    m_maxTorqueKNm = maxTorque;
    m_maxBladeMoment = maxBladeMoment;
}
//监测风机的机械载荷（扭矩、叶片弯矩）是否超过安全阈值，防止设备损坏。

//核心逻辑：

//构造函数接收扭矩上限和弯矩上限（来自配置文件）。

//isSafe()：遍历所有风机，如果任一风机的扭矩 > 上限×1.1 或弯矩 > 上限×1.1，返回false（不安全）。

//setLimits()：动态修改安全阈值。