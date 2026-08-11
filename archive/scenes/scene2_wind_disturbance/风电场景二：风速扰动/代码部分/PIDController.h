#ifndef PIDCONTROLLER_H
#define PIDCONTROLLER_H

class PIDController {
private:
    double kp, ki, kd;
    double integral;
    double prevError;
    double minOut, maxOut;

public:
    PIDController(double p, double i, double d, double minO, double maxO)
        : kp(p), ki(i), kd(d), minOut(minO), maxOut(maxO) {
        integral = 0.0;
        prevError = 0.0;
    }

    double compute(double setpoint, double processVar, double dt) {
        double error = setpoint - processVar;
        double p = kp * error;

        integral += error * dt;
        if (integral > 3.0) integral = 3.0;
        if (integral < -3.0) integral = -3.0;
        double i = ki * integral;

        double d = kd * (error - prevError) / dt;
        prevError = error;

        double out = p + i + d;
        if (out < minOut) return minOut;
        if (out > maxOut) return maxOut;
        return out;
    }

    void reset() {
        integral = 0.0;
        prevError = 0.0;
    }
};
#endif