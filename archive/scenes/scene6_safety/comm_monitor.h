#pragma once
#include <cstdio>

// ====== 通信监控器 (章渲祺) ======
class CommunicationMonitor {
public:
    void setHealthy(bool h) {
        if (h != healthy_) {
            healthy_ = h;
            std::printf("[通信] 状态变更: %s\n", healthy_ ? "正常" : "中断");
        }
    }
    bool isHealthy() const { return healthy_; }
private:
    bool healthy_ = true;
};
