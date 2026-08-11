// ============================================================================
// capacity_evaluator.h
// ============================================================================
#ifndef CAPACITY_EVALUATOR_H
#define CAPACITY_EVALUATOR_H

#include "agc_types.h"

/**
 * @class CapacityEvaluator
 * @brief 全场功率容量评估器
 *
 * 遍历所有风机，根据各自状态和安全指数计算单机功率上下限，
 * 累加得到全场最大可发功率与最小技术出力。
 */
class CapacityEvaluator {
public:
    /**
     * @brief 执行容量评估
     * @param farm      全场状态
     * @param total_max [输出] 全场最大可发功率 (MW)
     * @param total_min [输出] 全场最小技术出力 (MW)
     */
    static void evaluate(const FarmState& farm, double& total_max, double& total_min);
};

#endif // CAPACITY_EVALUATOR_H