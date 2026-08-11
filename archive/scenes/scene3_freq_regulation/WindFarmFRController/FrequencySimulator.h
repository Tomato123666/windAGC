#ifndef FREQ_SIM_H
#define FREQ_SIM_H

#include "FR_Types.h"

class FrequencySimulator
{
private:
    double freq = 50.0;
    double extraPower = 0.0; // 额外调频功率
    bool disturbance = false;

public:
    // 传入额外调频功率，直接拉频率
    double getFrequency(double frPower)
    {
        extraPower = frPower;

        if (!disturbance)
        {
            freq = 50.0;
        }
        else
        {
            // 额外功率越大，频率回升越快
            freq += extraPower / 2000.0;

            // 频率限幅，防止太离谱
            if (freq > 50.2) freq = 50.2;
            if (freq < 49.5) freq = 49.5;
        }

        return freq;
    }

    // 触发扰动：瞬间跌频
    void triggerDisturbance(double f)
    {
        disturbance = true;
        freq = f;
    }

    void reset()
    {
        disturbance = false;
        extraPower = 0.0;
    }
};

#endif