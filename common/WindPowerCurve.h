/**
 * @file WindPowerCurve.h
 * @brief 统一风力功率曲线模型
 *
 * 合并场景1(Turbine)、场景2(WindTurbineController)、场景5(IndexCalculator)的功率曲线实现。
 *
 * 支持:
 * - 二次方模型（场景1: P = rated * (v/rated)^2）
 * - 三次方模型（场景2/5: P = rated * (v/rated)^3）
 * - 线性插值模型（场景5: 3~12m/s线性）
 *
 * 三段式: 低于切入→0, 切入~额定→计算, 额定~切出→额定, 高于切出→0
 */
#pragma once

#include <algorithm>
#include <cmath>

namespace common {

enum class PowerCurveModel {
    CUBIC,      // P ~ v³ (贝茨极限理论)
    QUADRATIC,  // P ~ v² (现代变桨风机近似)
    LINEAR      // 线性插值
};

class WindPowerCurve {
public:
    /**
     * @param ratedPower 额定功率 (MW)
     * @param cutInSpeed 切入风速 (m/s), 默认3.0
     * @param ratedSpeed 额定风速 (m/s), 默认12.0
     * @param cutOutSpeed 切出风速 (m/s), 默认25.0
     * @param model 功率曲线模型, 默认CUBIC
     */
    WindPowerCurve(double ratedPower = 2.5,
                   double cutInSpeed = 3.0,
                   double ratedSpeed = 12.0,
                   double cutOutSpeed = 25.0,
                   PowerCurveModel model = PowerCurveModel::CUBIC)
        : ratedPower_(ratedPower), cutIn_(cutInSpeed),
          rated_(ratedSpeed), cutOut_(cutOutSpeed), model_(model)
    {}

    /**
     * 获取给定风速下的理想功率 (MW)
     */
    double getPower(double windSpeed) const {
        if (windSpeed < cutIn_ || windSpeed >= cutOut_) {
            return 0.0;
        }
        if (windSpeed >= rated_) {
            return ratedPower_;
        }
        // 切入 < 风速 < 额定 → 按模型计算
        double ratio = (windSpeed - cutIn_) / (rated_ - cutIn_);
        switch (model_) {
            case PowerCurveModel::QUADRATIC:
                return ratedPower_ * ratio * ratio;
            case PowerCurveModel::LINEAR:
                return ratedPower_ * ratio;
            case PowerCurveModel::CUBIC:
            default: {
                // P = rated * (v/v_rated)^3
                double vr = windSpeed / rated_;
                return ratedPower_ * vr * vr * vr;
            }
        }
    }

    /**
     * 获取考虑效率损失的实际功率
     * @param windSpeed 风速 (m/s)
     * @param pitchAngle 当前桨距角 (deg)
     * @param maxPitch 最大桨距角 (deg), 默认8.0
     * @param pitchEfficiencyLoss 全桨距角时的效率损失比例, 默认0.22
     * @return 实际功率 (MW)
     */
    double getActualPower(double windSpeed, double pitchAngle,
                          double maxPitch = 8.0, double pitchEfficiencyLoss = 0.22) const {
        double ideal = getPower(windSpeed);
        if (ideal <= 0.0) return 0.0;

        // 桨距角效率: pitch=0 → eff=1.0, pitch=maxPitch → eff=1.0-loss
        double pitchRatio = std::clamp(pitchAngle / maxPitch, 0.0, 1.0);
        double pitchEff = 1.0 - pitchRatio * pitchEfficiencyLoss;
        pitchEff = std::clamp(pitchEff, 0.8, 1.0);

        return ideal * pitchEff;
    }

    /**
     * 估算功率变化量
     * @param v1 当前风速
     * @param v2 预期风速
     * @return 预期功率变化 (MW)
     */
    double estimateDeltaP(double v1, double v2) const {
        return getPower(v2) - getPower(v1);
    }

    // 属性访问
    double ratedPower() const { return ratedPower_; }
    double cutInSpeed() const { return cutIn_; }
    double ratedSpeed() const { return rated_; }
    double cutOutSpeed() const { return cutOut_; }
    void setModel(PowerCurveModel m) { model_ = m; }

private:
    double ratedPower_;
    double cutIn_;
    double rated_;
    double cutOut_;
    PowerCurveModel model_;
};

} // namespace common
