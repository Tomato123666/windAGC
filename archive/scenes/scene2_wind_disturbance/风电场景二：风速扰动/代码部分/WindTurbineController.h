#ifndef WINDTURBINECONTROLLER_H
#define WINDTURBINECONTROLLER_H
#include "PIDController.h"
#include <cmath>

struct TurbineStatus {
    int turbineId;
    double windSpeed;
    double rotorSpeed;
    double pitchAngle;
    double actualPower;
    double targetPower;
    double adjustMarginUp;
    double adjustMarginDown;
    double maxAvailablePower;
    double speedSetpoint;
};

class WindTurbineController {
private:
    int m_turbineId;
    TurbineStatus m_status;
    PIDController m_powerPID;
    PIDController m_speedPID;

    const double RATED_SPEED = 1800.0;
    const double MIN_SPEED = 1750.0;
    const double MAX_PITCH = 8.0;
    const double MIN_PITCH = 0.0;
    const double PITCH_RATE = 0.3;
    const double RATED_POWER = 2.5;
    const double RATED_WIND = 12.0;
    const double CUT_IN = 3.0;

    template<typename T> T clamp(T v, T lo, T hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    double calcMaxPower() {
        if (m_status.windSpeed < CUT_IN) return 0.0;
        if (m_status.windSpeed >= RATED_WIND) return RATED_POWER;
        return RATED_POWER * pow(m_status.windSpeed / RATED_WIND, 3);
    }

    double calcActualPower() {
        double maxP = calcMaxPower();
        double pitchEff = 1.0 - (m_status.pitchAngle / MAX_PITCH) * 0.22;
        pitchEff = clamp(pitchEff, 0.8, 1.0);
        double speedEff = m_status.rotorSpeed / RATED_SPEED;
        speedEff = clamp(speedEff, 0.95, 1.0);
        return clamp(maxP * pitchEff * speedEff, 0.0, RATED_POWER);
    }

    void calcMargins() {
        m_status.maxAvailablePower = calcMaxPower();
        m_status.adjustMarginUp = clamp(m_status.maxAvailablePower - m_status.actualPower, 0.0, 2.0);
        m_status.adjustMarginDown = clamp(m_status.actualPower - 1.0, 0.0, 2.0);
    }

    void updateSpeed(double dt) {
        double speedCmd = m_speedPID.compute(m_status.speedSetpoint, m_status.rotorSpeed, dt);
        m_status.rotorSpeed += speedCmd * dt;
        m_status.rotorSpeed = clamp(m_status.rotorSpeed, MIN_SPEED, RATED_SPEED);
    }

public:
    WindTurbineController(int id)
        : m_turbineId(id),
        // ✅ 核心强化：PID参数增强，精准追踪10MW
        m_powerPID(1.8, 0.06, 0.04, -0.5, 0.5),
        m_speedPID(10.0, 0.2, 0.1, -25.0, 25.0)
    {
        m_status = { id, 12.0, 1800.0, 0.0, 0.0, 2.5, 0.0, 0.0, 0.0, 1800.0 };
        m_status.actualPower = calcActualPower();
        calcMargins();
    }

    void setWind(double ws) { m_status.windSpeed = ws; }
    void setTarget(double p) { m_status.targetPower = clamp(p, 1.0, RATED_POWER); }

    void execute(double dt, bool isWindRise) {
        double pitchCmd = m_powerPID.compute(m_status.targetPower, m_status.actualPower, dt);
        double deltaPitch = pitchCmd * 0.25;
        deltaPitch = clamp(deltaPitch, -PITCH_RATE * dt, PITCH_RATE * dt);
        m_status.pitchAngle = clamp(m_status.pitchAngle + deltaPitch, MIN_PITCH, MAX_PITCH);

        m_status.speedSetpoint = (m_status.windSpeed > 13.0) ? 1790.0 : 1800.0;
        updateSpeed(dt);

        m_status.actualPower = calcActualPower();
        calcMargins();
    }

    void preFluct(bool willRise) {
        m_status.pitchAngle = clamp(m_status.pitchAngle + 0.3, 0.0, 1.0);
        m_status.targetPower = 2.5;
        m_status.actualPower = calcActualPower();
        calcMargins();
    }

    void resetSmooth() {
        m_status.pitchAngle = clamp(m_status.pitchAngle - 0.1, 0.0, MAX_PITCH);
        m_status.rotorSpeed = 1800.0;
        m_status.speedSetpoint = 1800.0;
        m_status.targetPower = 2.5;
        m_status.actualPower = calcActualPower();
        calcMargins();
    }

    TurbineStatus status() { return m_status; }
};
#endif