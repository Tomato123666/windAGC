#ifndef POWER_CALC_H
#define POWER_CALC_H

#include "FR_Types.h"

class PowerCalculator
{
private:
    double Kp = 30.0;
    double Ki = 0.8;
    double integral = 0.0;
    double maxPower = 1500.0; // 锟斤拷锟斤拷频锟斤拷锟斤拷

public:
    double calculate(double df, double df_dt)
    {
        double p_prop = -Kp * df;

        if (fabs(df) > 0.005)
            integral += df * 0.2;

        double p_integ = -Ki * integral;
        double total = p_prop + p_integ;

        // 锟斤拷锟斤拷锟睫凤拷
        if (total > maxPower) total = maxPower;
        if (total < -maxPower) total = -maxPower;

        return total;
    }

    double getIntegral() const { return integral; }
    void reset()
    {
        integral = 0.0;
    }
};

#endif