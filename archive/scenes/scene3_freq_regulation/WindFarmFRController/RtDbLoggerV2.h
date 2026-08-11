/**
 * @file RtDbLoggerV2.h
 * @brief 场景3 一次调频 —— 统一架构版共享内存日志记录器
 *
 * 继承 common::RtDbLoggerBase，实现 buildPointIndexMap() 注册本场景数据点，
 * 并通过后台线程异步将缓存数据周期性写入共享内存，降低控制循环抖动。
 *
 * 数据点覆盖:
 *   - GRID.Frequency, GRID.FrequencyDelta
 *   - FR_CTRL.State, .TotalFRPower, .DeltaF, .Proportional, .Integral
 *   - TURBINE_000~009: .Power, .FR_Power, .Command  (10台 x 3点 = 30点)
 *
 * 接口兼容原有 RtDbLogger，FRController.h 可直接替换使用:
 *   将 #include "RtDbLogger.h" 改为 #include "RtDbLoggerV2.h"
 *   将 RtDbLogger*  改为 RtDbLoggerV2*
 *
 * 版本: 2.0
 * 日期: 2026-07-21
 */
#pragma once

#include "../../common/RtDbLoggerBase.h"
#include "../../common/CommonTypes.h"

#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <cstdio>

// ============================================================================
// RtDbLoggerV2
// ============================================================================

class RtDbLoggerV2 : public common::RtDbLoggerBase {
public:
    // ---- 构造 / 析构 ----

    RtDbLoggerV2();
    ~RtDbLoggerV2() override;

    // ---- 生命周期（增强基类，管理后台线程） ----

    /**
     * @brief  初始化: 连接共享内存 → 构建数据点索引 → 启动后台日志线程
     * @return true=共享内存连通, false=降级运行 (仅控制台输出，不影响控制逻辑)
     */
    bool initialize();

    /**
     * @brief  清理: 停止后台线程 → 断开共享内存
     */
    void cleanup();

    // ---- 便捷日志方法（接口与 RtDbLogger / FRController.h 完全兼容） ----

    /**
     * @brief  每个控制周期调用: 写入电网频率、状态机状态、总调频功率及PID分量
     * @param frequency      电网频率 (Hz)
     * @param deltaF         频率偏差 (Hz) —— 同步写入 GRID.FrequencyDelta 和 FR_CTRL.DeltaF
     * @param dfDt           频率变化率 (Hz/s)
     * @param state          状态机当前状态 (0=READY, 1=ACTIVE, 2=RECOVERY, 3=FAULT)
     * @param totalFRPower   全场总调频功率 (kW)
     * @param proportionalKW 比例分量 (kW)
     * @param integralKW     积分分量 (kW)
     */
    void logFRCycle(double frequency, double deltaF, double dfDt,
                    int state, double totalFRPower,
                    double proportionalKW, double integralKW);

    /**
     * @brief  写入单台风机调频指令
     * @param turbineId  风机编号 (1~10, 1-based; 越界静默忽略)
     * @param basePower  基值功率 (kW)
     * @param frPower    调频增量 (kW)
     * @param finalCmd   最终功率指令 (kW) = basePower + frPower
     * @param running    风机是否运行 (保留以兼容旧接口，当前实现未使用)
     */
    void logTurbineFR(int turbineId, double basePower, double frPower,
                      double finalCmd, bool running = true);

    /**
     * @brief  记录一次频率扰动事件 (立即写入共享内存 + 控制台输出)
     * @param triggerFreq  触发扰动时的电网频率 (Hz)
     */
    void logDisturbance(double triggerFreq);

    /**
     * @brief  记录状态机状态切换事件 (立即写入共享内存 + 控制台输出)
     * @param oldState  旧状态 (0=READY, 1=ACTIVE, 2=RECOVERY, 3=FAULT)
     * @param newState  新状态
     * @param reason    切换原因 (如 "频率越限", "频率恢复", "故障触发"; nullptr 则显示 "自动")
     */
    void logStateChange(int oldState, int newState, const char* reason);

    // ---- 后台线程控制 ----

    /**
     * @brief  启动后台日志线程 (initialize() 内部自动调用，一般无需手动调用)
     * @param intervalMs  刷写间隔 (ms), 默认 500ms
     */
    void startBackgroundLogging(int intervalMs = 500);

    /**
     * @brief  停止后台日志线程 (cleanup() 内部自动调用)
     */
    void stopBackgroundLogging();

    // ---- 查询 ----

    /** 后台线程是否正在运行 */
    bool isBackgroundRunning() const {
        return bgRunning_.load(std::memory_order_acquire);
    }

protected:
    // ---- 实现基类纯虚函数 ----

    /**
     * @brief  注册场景3所有共享内存数据点索引
     *
     * 由基类 initialize() 在连接成功后自动调用。
     * 注册点 (共37个):
     *   GRID:       Frequency, FrequencyDelta
     *   FR_CTRL:    State, TotalFRPower, DeltaF, Proportional, Integral
     *   TURBINE_*:  Power, FR_Power, Command  (10 x 3)
     */
    void buildPointIndexMap() override;

private:
    // ---- 后台线程主循环 ----
    void backgroundLoop();

    // ================================================================
    // 数据点索引 (run-time 由 buildPointIndexMap() 填充)
    // ================================================================

    // GRID
    size_t idxGridFreq_      = static_cast<size_t>(-1);
    size_t idxGridFreqDelta_ = static_cast<size_t>(-1);

    // FR_CTRL
    size_t idxFRState_       = static_cast<size_t>(-1);
    size_t idxFRTotalPower_  = static_cast<size_t>(-1);
    size_t idxFRDeltaF_      = static_cast<size_t>(-1);
    size_t idxFRProp_        = static_cast<size_t>(-1);
    size_t idxFRInteg_       = static_cast<size_t>(-1);

    // 风机 (TURBINE_000 ~ TURBINE_009)
    static const int MAX_TURB = 10;
    size_t idxTurbPower_[MAX_TURB];
    size_t idxTurbFRPower_[MAX_TURB];
    size_t idxTurbCmd_[MAX_TURB];

    // ================================================================
    // 后台线程
    // ================================================================
    std::thread       bgThread_;
    std::atomic<bool> bgRunning_{false};
    std::atomic<int>  bgIntervalMs_{500};

    // ================================================================
    // 数据缓存 (受 cacheMutex_ 保护)
    //
    // 控制循环调用 log*() 方法仅更新此缓存，后台线程定期将快照刷入
    // 共享内存。这样控制循环的写入延迟是 O(1) 的 cache 更新，不阻塞
    // 在 RT_DB 的跨进程调用上。
    // ================================================================
    mutable std::mutex cacheMutex_;

    struct CachedData {
        double freq       = 50.0;     // 电网频率
        double freqDelta  = 0.0;      // 频率偏差
        double dfDt       = 0.0;      // 频率变化率
        int    state      = 0;        // READY
        double totalFR    = 0.0;      // 全场总调频功率
        double propKW     = 0.0;      // 比例分量
        double integKW    = 0.0;      // 积分分量

        // 风机数据 (0-based 索引)
        double turbPower[MAX_TURB] = {};
        double turbFR[MAX_TURB]    = {};
        double turbCmd[MAX_TURB]   = {};
    };
    CachedData cache_;
};

// ============================================================================
// 内联实现
// ============================================================================

inline RtDbLoggerV2::RtDbLoggerV2()
    : common::RtDbLoggerBase("FR_CTRL_V2")
{
    // 风机索引初始化为无效值
    for (int i = 0; i < MAX_TURB; ++i) {
        idxTurbPower_[i]  = static_cast<size_t>(-1);
        idxTurbFRPower_[i] = static_cast<size_t>(-1);
        idxTurbCmd_[i]    = static_cast<size_t>(-1);
    }
}

inline RtDbLoggerV2::~RtDbLoggerV2()
{
    // 先停后台线程，再让基类析构调用 cleanup()
    stopBackgroundLogging();
}

// ---- 生命周期 ----

inline bool RtDbLoggerV2::initialize()
{
    // 基类 initialize() 连接 RT_DB 并在成功后调用我们的 buildPointIndexMap()
    bool ok = common::RtDbLoggerBase::initialize();

    // 启动后台日志线程 (共享内存不通时仍启动，降级为 console-only)
    startBackgroundLogging();

    if (ok) {
        std::printf("[FR_CTRL_V2] RT_DB 连接成功, 后台日志线程已启动\n");
    } else {
        std::printf("[FR_CTRL_V2] 共享内存不可用, 仅控制台日志模式\n");
    }

    // 无论共享内存是否可用，都返回 true 让上层继续运行
    return ok;
}

inline void RtDbLoggerV2::cleanup()
{
    stopBackgroundLogging();
    common::RtDbLoggerBase::cleanup();
}

// ---- buildPointIndexMap ----

inline void RtDbLoggerV2::buildPointIndexMap()
{
    // ── GRID 频率数据 (2点) ──
    registerPoint("GRID.Frequency",      &idxGridFreq_);
    registerPoint("GRID.FrequencyDelta", &idxGridFreqDelta_);

    // ── FR_CTRL 状态与功率 (5点) ──
    registerPoint("FR_CTRL.State",        &idxFRState_);
    registerPoint("FR_CTRL.TotalFRPower", &idxFRTotalPower_);
    registerPoint("FR_CTRL.DeltaF",       &idxFRDeltaF_);
    registerPoint("FR_CTRL.Proportional", &idxFRProp_);
    registerPoint("FR_CTRL.Integral",     &idxFRInteg_);

    // ── TURBINE_000 ~ TURBINE_009 风机数据 (10 x 3 = 30点) ──
    for (int t = 0; t < MAX_TURB; ++t) {
        char buf[64];

        std::snprintf(buf, sizeof(buf), "TURBINE_%03d.Power", t);
        registerPoint(buf, &idxTurbPower_[t]);

        std::snprintf(buf, sizeof(buf), "TURBINE_%03d.FR_Power", t);
        registerPoint(buf, &idxTurbFRPower_[t]);

        std::snprintf(buf, sizeof(buf), "TURBINE_%03d.Command", t);
        registerPoint(buf, &idxTurbCmd_[t]);
    }
}

// ---- 便捷日志方法 ----

inline void RtDbLoggerV2::logFRCycle(double frequency, double deltaF, double dfDt,
                                      int state, double totalFRPower,
                                      double proportionalKW, double integralKW)
{
    // 更新缓存快照 (O(1) 锁临界区)
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        cache_.freq      = frequency;
        cache_.freqDelta = deltaF;
        cache_.dfDt      = dfDt;
        cache_.state     = state;
        cache_.totalFR   = totalFRPower;
        cache_.propKW    = proportionalKW;
        cache_.integKW   = integralKW;
    }

    // 后台线程未运行时的 fallback: 直接写入共享内存
    if (!bgRunning_.load(std::memory_order_acquire)) {
        writePoint(idxGridFreq_,      frequency);
        writePoint(idxGridFreqDelta_, deltaF);
        writePoint(idxFRState_,       static_cast<double>(state));
        writePoint(idxFRTotalPower_,  totalFRPower);
        writePoint(idxFRDeltaF_,      deltaF);
        writePoint(idxFRProp_,        proportionalKW);
        writePoint(idxFRInteg_,       integralKW);
    }
}

inline void RtDbLoggerV2::logTurbineFR(int turbineId, double basePower,
                                        double frPower, double finalCmd,
                                        bool running)
{
    // 1-based → 0-based; 越界静默忽略
    if (turbineId < 1 || turbineId > MAX_TURB) return;
    int i = turbineId - 1;

    // 更新缓存
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        cache_.turbPower[i] = basePower;
        cache_.turbFR[i]    = frPower;
        cache_.turbCmd[i]   = finalCmd;
    }

    // Fallback: 后台线程未运行时直接写入
    if (!bgRunning_.load(std::memory_order_acquire)) {
        writePoint(idxTurbPower_[i],  basePower);
        writePoint(idxTurbFRPower_[i], frPower);
        writePoint(idxTurbCmd_[i],    finalCmd);
    }

    // running 参数保留以兼容 FRController.h 调用签名
    (void)running;
}

inline void RtDbLoggerV2::logDisturbance(double triggerFreq)
{
    // 扰动事件立即写入 (不等待后台线程)
    writePoint(idxGridFreq_, triggerFreq);

    // 同步更新缓存中的频率值
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        cache_.freq = triggerFreq;
    }

    std::printf("[FR_CTRL_V2] >>> 频率扰动事件: %.3f Hz <<<\n", triggerFreq);
}

inline void RtDbLoggerV2::logStateChange(int oldState, int newState,
                                          const char* reason)
{
    // 状态切换事件立即写入
    writePoint(idxFRState_, static_cast<double>(newState));

    // 同步更新缓存
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        cache_.state = newState;
    }

    // 使用 common::frStateStr 将枚举值转为可读中文
    const char* oldStr = common::frStateStr(static_cast<common::FRState>(oldState));
    const char* newStr = common::frStateStr(static_cast<common::FRState>(newState));

    std::printf("[FR_CTRL_V2] 状态切换: %s -> %s  原因: %s\n",
                oldStr, newStr, reason ? reason : "自动");
}

// ---- 后台线程控制 ----

inline void RtDbLoggerV2::startBackgroundLogging(int intervalMs)
{
    // 防止重复启动
    if (bgRunning_.load(std::memory_order_acquire)) {
        stopBackgroundLogging();
    }

    bgIntervalMs_.store(intervalMs, std::memory_order_release);
    bgRunning_.store(true, std::memory_order_release);

    bgThread_ = std::thread(&RtDbLoggerV2::backgroundLoop, this);
}

inline void RtDbLoggerV2::stopBackgroundLogging()
{
    bgRunning_.store(false, std::memory_order_release);

    if (bgThread_.joinable()) {
        bgThread_.join();
    }
}

inline void RtDbLoggerV2::backgroundLoop()
{
    std::printf("[FR_CTRL_V2] 后台日志线程已启动 (刷新间隔 = %d ms)\n",
                bgIntervalMs_.load(std::memory_order_relaxed));

    while (bgRunning_.load(std::memory_order_acquire)) {

        // ---- 取缓存快照 (临界区极短) ----
        CachedData snap;
        {
            std::lock_guard<std::mutex> lock(cacheMutex_);
            snap = cache_;
        }

        // ---- 刷入共享内存 ----
        if (isConnected()) {
            // GRID
            writePoint(idxGridFreq_,      snap.freq);
            writePoint(idxGridFreqDelta_, snap.freqDelta);

            // FR_CTRL
            writePoint(idxFRState_,       static_cast<double>(snap.state));
            writePoint(idxFRTotalPower_,  snap.totalFR);
            writePoint(idxFRDeltaF_,      snap.freqDelta);   // 与 GRID.FrequencyDelta 同值
            writePoint(idxFRProp_,        snap.propKW);
            writePoint(idxFRInteg_,       snap.integKW);

            // TURBINE_000 ~ TURBINE_009
            for (int i = 0; i < MAX_TURB; ++i) {
                writePoint(idxTurbPower_[i],  snap.turbPower[i]);
                writePoint(idxTurbFRPower_[i], snap.turbFR[i]);
                writePoint(idxTurbCmd_[i],    snap.turbCmd[i]);
            }
        }

        // ---- 等待下一周期 ----
        int ms = bgIntervalMs_.load(std::memory_order_relaxed);
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }

    std::printf("[FR_CTRL_V2] 后台日志线程已停止\n");
}
