// ============================================================================
// classifier.h
// ============================================================================
#ifndef CLASSIFIER_H
#define CLASSIFIER_H

#include <vector>
#include "agc_types.h"

/**
 * @class Classifier
 * @brief 风机角色分类器
 *
 * 在 NORMAL_TRACKING 模式下，根据各风机的性能指数 P 和机动指数 M，
 * 动态划分基荷机组(BASE)与调节机组(REGULATING)。
 * 深度限电模式下，所有健康风机强制设为 BASE。
 */
class Classifier {
public:
    /**
     * @brief 构造函数，绑定全局配置
     * @param config 全局AGC参数，读取 k_attenuate
     */
    explicit Classifier(const AgcConfig& config);

    /**
     * @brief 执行角色分配
     * @param turbines               全场风机状态列表（将被修改）
     * @param mode                   当前运行模式
     * @param required_reg_capacity  所需调节容量 (MW)，负数表示自动按总可用功率的20%计算
     */
    void classify(std::vector<TurbineFullState>& turbines,
        OperationMode mode,
        double required_reg_capacity = -1.0);

private:
    const AgcConfig& cfg_;
};

#endif // CLASSIFIER_H