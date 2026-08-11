/**
 * @file RtDbLoggerV2.h
 * @brief 场景5（限电备用优化）共享内存日志记录器
 *
 * 继承 common::RtDbLoggerBase，实现 buildPointIndexMap() 注册场景5所需
 * 的 RT_DB 数据点，并提供便捷的日志方法。可直接嵌入 WindFarmAgc::step() 管线。
 *
 * 数据点分组:
 *   CURTAIL.*       — 限电参数（比例、模式、可用功率、备用等）
 *   REST_ROTATE.*   — 轮休调度（热备用台数、运行台数）
 *   PV_AGC.*        — 光伏 AGC 交互（总功率、SOC、模式）
 *   WIND_AGC.*      — 风电 AGC 输出（总功率、指令值）
 */
#pragma once

#include "../../../../common/RtDbLoggerBase.h"
#include "../../../../common/CommonTypes.h"

#include <cstdio>
#include <cstring>
#include <sstream>

/**
 * @class RtDbLoggerV2
 * @brief 场景5限电备用优化的 RT_DB 日志记录器
 *
 * 使用方式:
 *   1. 构造对象
 *   2. 调用 initialize() 连接共享内存并注册数据点
 *   3. 在 WindFarmAgc::step() 中调用 logAgcStep() 或各便捷方法
 *   4. 析构时自动断开连接（由基类处理）
 */
class RtDbLoggerV2 : public common::RtDbLoggerBase {
public:
    // ========================================================================
    // 构造 / 析构
    // ========================================================================

    explicit RtDbLoggerV2(const char* sceneName = "Scene5_Curtailment")
        : common::RtDbLoggerBase(sceneName)
    {}

    // ========================================================================
    // 高层接口：一次调用写入所有场景5数据点
    // 设计为 WindFarmAgc::step() 在每个控制周期末尾调用
    // ========================================================================

    /**
     * @brief 记录一个 AGC 控制周期的全部关键数据
     *
     * @param curtailRatio       限电比例 (0~1)
     * @param curtailMode        限电模式（对应 common::CurtailMode 枚举值）
     * @param totalAvailable     全场可用功率 (MW)
     * @param totalMinTech       全场最小技术出力 (MW)
     * @param efficientPower     全场高效功率 (MW)
     * @param reserveUp          上调备用 (MW)
     * @param reserveDown        下调备用 (MW)
     * @param hotStandbyCount    热备用风机数量
     * @param activeCount        运行中风机数量
     * @param windTotalPower     风电实际总功率 (MW)
     * @param windSetpoint       风电指令总功率 (MW)
     * @param pvTotalPower       光伏总功率 (MW)
     * @param pvSOC              光伏储能 SOC (0~1)
     * @param pvMode             光伏模式
     */
    void logAgcStep(double curtailRatio,
                    int    curtailMode,
                    double totalAvailable,
                    double totalMinTech,
                    double efficientPower,
                    double reserveUp,
                    double reserveDown,
                    int    hotStandbyCount,
                    int    activeCount,
                    double windTotalPower,
                    double windSetpoint,
                    double pvTotalPower,
                    double pvSOC,
                    int    pvMode)
    {
        // --- CURTAIL 组 ---
        writePoint(idxCurtailRatio_,           curtailRatio);
        writePoint(idxCurtailMode_,            static_cast<double>(curtailMode));
        writePoint(idxCurtailTotalAvailable_,  totalAvailable);
        writePoint(idxCurtailTotalMinTech_,    totalMinTech);
        writePoint(idxCurtailEfficientPower_,  efficientPower);
        writePoint(idxCurtailReserveUp_,       reserveUp);
        writePoint(idxCurtailReserveDown_,     reserveDown);

        // --- REST_ROTATE 组 ---
        writePoint(idxRestRotateHotStandbyCount_, static_cast<double>(hotStandbyCount));
        writePoint(idxRestRotateActiveCount_,     static_cast<double>(activeCount));

        // --- PV_AGC 组 ---
        writePoint(idxPvAgcTotalPower_, pvTotalPower);
        writePoint(idxPvAgcSOC_,        pvSOC);
        writePoint(idxPvAgcMode_,       static_cast<double>(pvMode));

        // --- WIND_AGC 组 ---
        writePoint(idxWindAgcTotalPower_, windTotalPower);
        writePoint(idxWindAgcSetpoint_,   windSetpoint);
    }

    // ========================================================================
    // 便捷方法
    // ========================================================================

    /**
     * @brief 记录限电步骤的完整参数
     *
     * 在一次限电决策后调用，写入 CURTAIL.* 组的所有数据点。
     *
     * @param curtailRatio    限电比例 (0~1)
     * @param mode            限电模式（int，对应 CurtailMode 枚举）
     * @param totalAvail      全场可用功率 (MW)
     * @param reserveUp       上调备用 (MW)
     * @param reserveDown     下调备用 (MW)
     * @param hotStandbyCount 当前热备用风机数量
     */
    void logCurtailStep(double curtailRatio,
                        int    mode,
                        double totalAvail,
                        double reserveUp,
                        double reserveDown,
                        int    hotStandbyCount)
    {
        writePoint(idxCurtailRatio_,           curtailRatio);
        writePoint(idxCurtailMode_,            static_cast<double>(mode));
        writePoint(idxCurtailTotalAvailable_,  totalAvail);
        writePoint(idxCurtailReserveUp_,       reserveUp);
        writePoint(idxCurtailReserveDown_,     reserveDown);
        writePoint(idxRestRotateHotStandbyCount_, static_cast<double>(hotStandbyCount));
    }

    /**
     * @brief 记录运行模式切换事件
     *
     * 在 ModeSelector 触发模式变更时调用。
     *
     * @param fromMode  切换前模式（int，对应 common::OperationMode 枚举值）
     * @param toMode    切换后模式（int，对应 common::OperationMode 枚举值）
     * @param reason    切换原因描述
     */
    void logModeTransition(int fromMode, int toMode, const char* reason)
    {
        // 写入 CURTAIL.Mode 表示当前限电相关模式
        writePoint(idxCurtailMode_, static_cast<double>(toMode));

        // 同时输出控制台日志便于调试
        if (isConnected()) {
            std::printf("[%s] Mode transition: %d -> %d (%s)\n",
                        sceneName_, fromMode, toMode, reason);
        }
    }

    /**
     * @brief 批量记录所有风机状态
     *
     * 每个控制周期调用一次，将全场风机状态写入共享内存。
     * 注意：每条风机状态记录包含基本运行数据，具体写入策略取决于
     * RT_DB 中是否为每台风机预定义了独立数据点。如果共享内存中仅
     * 定义了聚合点，则本方法仅将首台风机的关键字段写入对应点。
     *
     * @param turbines  风机状态数组指针
     * @param count     风机数量
     */
    void logTurbineStates(const common::TurbineStatus* turbines, int count)
    {
        if (!turbines || count <= 0) return;

        // 聚合统计
        double totalAvailable = 0.0;
        double totalMinTech = 0.0;
        double totalEffPower = 0.0;
        int    hotStandbyCnt = 0;
        int    activeCnt = 0;

        for (int i = 0; i < count; ++i) {
            const auto& t = turbines[i];
            totalAvailable += t.powerAvailableMax;
            totalMinTech   += t.powerAvailableMin;
            totalEffPower  += t.efficientPower;

            switch (t.state) {
            case common::TurbineRunState::HOT_STANDBY:
                ++hotStandbyCnt;
                break;
            case common::TurbineRunState::STOPPED:
            case common::TurbineRunState::FAULT:
                break;
            default:
                ++activeCnt;
                break;
            }
        }

        writePoint(idxCurtailTotalAvailable_,  totalAvailable);
        writePoint(idxCurtailTotalMinTech_,    totalMinTech);
        writePoint(idxCurtailEfficientPower_,  totalEffPower);
        writePoint(idxRestRotateHotStandbyCount_, static_cast<double>(hotStandbyCnt));
        writePoint(idxRestRotateActiveCount_,     static_cast<double>(activeCnt));
    }

    /**
     * @brief 记录轮休调度事件
     *
     * 在 RestRotationScheduler 执行启停决策时调用。
     *
     * @param event      事件描述（如 "STOP", "START", "ROTATE"）
     * @param turbineId  涉及的风机编号
     */
    void logRotationEvent(const char* event, int turbineId)
    {
        if (isConnected()) {
            std::printf("[%s] Rotation event: %s on turbine %d\n",
                        sceneName_, event, turbineId);
        }

        // 生成日志字符串写入共享内存（如果有日志文本点则可扩展）
        std::ostringstream oss;
        oss << "ROTATION:" << event << ":WT" << turbineId;
        // 预留：可将事件字符串写入专用日志点
    }

    // ========================================================================
    // 直接访问点索引（供外部读取某个点使用）
    // ========================================================================

    size_t getCurtailRatioIdx()        const { return idxCurtailRatio_; }
    size_t getCurtailModeIdx()         const { return idxCurtailMode_; }
    size_t getCurtailTotalAvailIdx()   const { return idxCurtailTotalAvailable_; }
    size_t getCurtailTotalMinTechIdx() const { return idxCurtailTotalMinTech_; }
    size_t getCurtailEffPowerIdx()     const { return idxCurtailEfficientPower_; }
    size_t getCurtailReserveUpIdx()    const { return idxCurtailReserveUp_; }
    size_t getCurtailReserveDownIdx()  const { return idxCurtailReserveDown_; }
    size_t getHotStandbyCountIdx()     const { return idxRestRotateHotStandbyCount_; }
    size_t getActiveCountIdx()         const { return idxRestRotateActiveCount_; }
    size_t getPvAgcTotalPowerIdx()     const { return idxPvAgcTotalPower_; }
    size_t getPvAgcSOCIdx()            const { return idxPvAgcSOC_; }
    size_t getPvAgcModeIdx()           const { return idxPvAgcMode_; }
    size_t getWindAgcTotalPowerIdx()   const { return idxWindAgcTotalPower_; }
    size_t getWindAgcSetpointIdx()     const { return idxWindAgcSetpoint_; }

protected:
    // ========================================================================
    // buildPointIndexMap —— 注册场景5全部数据点
    // ========================================================================
    void buildPointIndexMap() override
    {
        // --- CURTAIL 组：限电核心参数 ---
        registerPoint("CURTAIL.Ratio",          &idxCurtailRatio_);
        registerPoint("CURTAIL.Mode",           &idxCurtailMode_);
        registerPoint("CURTAIL.TotalAvailable", &idxCurtailTotalAvailable_);
        registerPoint("CURTAIL.TotalMinTech",   &idxCurtailTotalMinTech_);
        registerPoint("CURTAIL.EfficientPower", &idxCurtailEfficientPower_);
        registerPoint("CURTAIL.ReserveUp",      &idxCurtailReserveUp_);
        registerPoint("CURTAIL.ReserveDown",    &idxCurtailReserveDown_);

        // --- REST_ROTATE 组：轮休调度状态 ---
        registerPoint("REST_ROTATE.HotStandbyCount", &idxRestRotateHotStandbyCount_);
        registerPoint("REST_ROTATE.ActiveCount",     &idxRestRotateActiveCount_);

        // --- PV_AGC 组：光伏 AGC 交互 ---
        registerPoint("PV_AGC.TotalPower", &idxPvAgcTotalPower_);
        registerPoint("PV_AGC.SOC",        &idxPvAgcSOC_);
        registerPoint("PV_AGC.Mode",       &idxPvAgcMode_);

        // --- WIND_AGC 组：风电 AGC 输出 ---
        registerPoint("WIND_AGC.TotalPower", &idxWindAgcTotalPower_);
        registerPoint("WIND_AGC.Setpoint",   &idxWindAgcSetpoint_);
    }

private:
    // ========================================================================
    // 数据点索引（(size_t)-1 表示未注册/未找到）
    // ========================================================================

    // CURTAIL 组
    size_t idxCurtailRatio_          = static_cast<size_t>(-1);
    size_t idxCurtailMode_           = static_cast<size_t>(-1);
    size_t idxCurtailTotalAvailable_ = static_cast<size_t>(-1);
    size_t idxCurtailTotalMinTech_   = static_cast<size_t>(-1);
    size_t idxCurtailEfficientPower_  = static_cast<size_t>(-1);
    size_t idxCurtailReserveUp_      = static_cast<size_t>(-1);
    size_t idxCurtailReserveDown_    = static_cast<size_t>(-1);

    // REST_ROTATE 组
    size_t idxRestRotateHotStandbyCount_ = static_cast<size_t>(-1);
    size_t idxRestRotateActiveCount_     = static_cast<size_t>(-1);

    // PV_AGC 组
    size_t idxPvAgcTotalPower_ = static_cast<size_t>(-1);
    size_t idxPvAgcSOC_        = static_cast<size_t>(-1);
    size_t idxPvAgcMode_       = static_cast<size_t>(-1);

    // WIND_AGC 组
    size_t idxWindAgcTotalPower_ = static_cast<size_t>(-1);
    size_t idxWindAgcSetpoint_   = static_cast<size_t>(-1);
};
