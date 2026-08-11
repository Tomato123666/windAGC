// ============================================================================
// index_calculator.h
// ============================================================================
#ifndef INDEX_CALCULATOR_H
#define INDEX_CALCULATOR_H

#include "agc_types.h"

/**
 * @class IndexCalculator
 * @brief 风机综合指数计算器
 *
 * 根据实时遥测数据、安全阈值和全局配置参数，计算单机的安全指数 S、
 * 性能指数 P 和机动指数 M。所有计算均在栈上完成，无动态内存分配。
 */
class IndexCalculator {
public:
    /**
     * @brief 构造函数，绑定全局配置
     * @param config 全局AGC参数，内部以引用保存，调用者需确保生命周期长于本对象
     */
    explicit IndexCalculator(const AgcConfig& config);

    /**
     * @brief 计算安全指数 S (0~1)
     * @param telem  风机遥测数据
     * @param safety 安全阈值
     * @return S 值，0 表示极度危险，1 表示完全安全
     */
    double computeS(const TurbineTelemetry& telem,
        const TurbineSafetyParams& safety) const;

    /**
     * @brief 计算性能指数 P (0~1)
     * @param telem 风机遥测数据
     * @return P 值，1 表示理想功率输出
     */
    double computeP(const TurbineTelemetry& telem) const;

    /**
     * @brief 计算机动指数 M (0~1)
     * @param telem 风机遥测数据
     * @return M 值，1 表示调节能力最强
     */
    double computeM(const TurbineTelemetry& telem) const;

private:
    /**
     * @brief 根据风速查询理想功率（MW）
     * @param wind_speed 机舱风速 (m/s)
     * @return 该风速下的理想功率 (MW)，超出切入切出范围时返回0
     *
     * 功率曲线采用简化线性模型：
     * 切入风速 3 m/s → 0 MW，额定风速 12 m/s → 额定功率，切出风速 25 m/s → 0 MW
     */
    double powerCurveLookup(double wind_speed) const;

    const AgcConfig& config_;   ///< 全局配置引用
};

#endif // INDEX_CALCULATOR_H