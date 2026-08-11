#ifndef PREDICTION_H
#define PREDICTION_H

#include <vector>
#include <cmath>
#include <algorithm>
#include <stdexcept>

struct PredictionResult {
    double currentWindSpeed;
    double futureWindSpeed;
    bool hasFluctuationWarning;
    bool hasStableWarning;
    bool windWillRise;
    bool windWillDrop;
    double expectedDeltaP;
};

class Prediction {
private:
    int m_totalSimSteps;
    int m_warningLeadSteps;
    int m_scenario; // 1=风速变大（阵风），2=风速变小（风衰减）
    std::vector<double> m_windSpeedSequence;

    // 生成符合平抑逻辑的风况序列
    inline void generateWindSequence() {
        m_windSpeedSequence.resize(m_totalSimSteps);
        double dt = 0.05; // 50ms/步

        if (m_scenario == 1) {
            // ================== 场景1：风速变大（阵风） ==================
            // 0-20s：稳定12m/s（额定风速，满发10MW）
            // 20-30s：10s内从12m/s跃升到15m/s（超额定，风能过剩，需平抑）
            // 30-60s：湍流波动14-16m/s
            // 60s后：稳定12m/s
            for (int i = 0; i < m_totalSimSteps; ++i) {
                double time = i * dt;
                if (time < 20.0) {
                    m_windSpeedSequence[i] = 12.0;
                }
                else if (time >= 20.0 && time < 30.0) {
                    m_windSpeedSequence[i] = 12.0 + (time - 20.0) * 0.3;
                }
                else if (time >= 30.0 && time < 60.0) {
                    double turbulence = sin(time * 2.0) * 1.0;
                    m_windSpeedSequence[i] = 15.0 + turbulence;
                }
                else {
                    m_windSpeedSequence[i] = 12.0;
                }
            }
        }
        else if (m_scenario == 2) {
            // ================== 场景2：风速变小（风衰减） ==================
            // 0-20s：稳定12m/s（额定风速，满发10MW）
            // 20-30s：10s内从12m/s衰减到10m/s（接近额定，需平抑）
            // 30-60s：湍流波动9-11m/s
            // 60s后：稳定12m/s
            for (int i = 0; i < m_totalSimSteps; ++i) {
                double time = i * dt;
                if (time < 20.0) {
                    m_windSpeedSequence[i] = 12.0;
                }
                else if (time >= 20.0 && time < 30.0) {
                    m_windSpeedSequence[i] = 12.0 - (time - 20.0) * 0.2;
                }
                else if (time >= 30.0 && time < 60.0) {
                    double turbulence = sin(time * 2.0) * 1.0;
                    m_windSpeedSequence[i] = 10.0 + turbulence;
                }
                else {
                    m_windSpeedSequence[i] = 12.0;
                }
            }
        }
        else {
            throw std::invalid_argument("Invalid scenario number!");
        }
    }

public:
    // 构造函数：1=风速变大，2=风速变小
    Prediction(int totalSimSteps, int warningLeadSteps = 200, int scenario = 1)
        : m_totalSimSteps(totalSimSteps), m_warningLeadSteps(warningLeadSteps), m_scenario(scenario) {
        generateWindSequence();
    }

    inline PredictionResult getPrediction(int currentStep) {
        PredictionResult result;
        if (currentStep >= m_totalSimSteps) {
            result.currentWindSpeed = 0.0;
            return result;
        }

        result.currentWindSpeed = m_windSpeedSequence[currentStep];
        int futureEnd = std::min(currentStep + m_warningLeadSteps, m_totalSimSteps - 1);
        double futureWindSum = 0.0;
        for (int i = currentStep; i <= futureEnd; ++i) {
            futureWindSum += m_windSpeedSequence[i];
        }
        result.futureWindSpeed = futureWindSum / (futureEnd - currentStep + 1);

        // 预警阈值：未来10s风速变化≥1m/s触发预警
        double windDelta = result.futureWindSpeed - result.currentWindSpeed;
        result.hasFluctuationWarning = (fabs(windDelta) >= 1.0);
        result.windWillRise = (windDelta > 0);
        result.windWillDrop = (windDelta < 0);
        result.expectedDeltaP = 10.0 * (pow(result.futureWindSpeed, 3) - pow(result.currentWindSpeed, 3)) / pow(12.0, 3);

        // 稳定预警：未来3s风速波动<0.3m/s
        int stableEnd = std::min(currentStep + 60, m_totalSimSteps - 1);
        double maxFutureWind = *std::max_element(m_windSpeedSequence.begin() + currentStep, m_windSpeedSequence.begin() + stableEnd);
        double minFutureWind = *std::min_element(m_windSpeedSequence.begin() + currentStep, m_windSpeedSequence.begin() + stableEnd);
        result.hasStableWarning = (maxFutureWind - minFutureWind < 0.3) && !result.hasFluctuationWarning;

        return result;
    }
};

#endif // PREDICTION_H