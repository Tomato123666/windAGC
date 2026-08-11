// ============================================================================
// command_checker.h
// ============================================================================
#ifndef COMMAND_CHECKER_H
#define COMMAND_CHECKER_H

#include "agc_types.h"

/**
 * @class CommandChecker
 * @brief 调度指令校核器
 *
 * 将调度下发的总目标功率与全场容量上下限进行比较，
 * 若越限则钳位并标记 capped。
 */
class CommandChecker {
public:
    /**
     * @brief 校核并钳位目标指令
     * @param target_cmd 调度原始目标 (MW)
     * @param total_max  全场最大可发功率 (MW)
     * @param total_min  全场最小技术出力 (MW)
     * @param capped     [输出] 是否发生越限钳位
     * @return 钳位后的安全目标值 (MW)
     */
    static double checkAndClamp(double target_cmd, double total_max,
        double total_min, bool& capped);
};

#endif // COMMAND_CHECKER_H