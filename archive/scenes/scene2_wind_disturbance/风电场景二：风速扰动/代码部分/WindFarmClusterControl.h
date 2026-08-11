#ifndef WINDFARMCLUSTERCONTROL_H
#define WINDFARMCLUSTERCONTROL_H
#include "WindTurbineController.h"
#include <vector>

struct FarmStatus {
    double totalTarget;
    double totalActual;
    double totalUp;
    double totalDown;
    double avgPitch;
    double avgSpeed;
    int count;
};

class WindFarmClusterControl {
private:
    std::vector<WindTurbineController*> m_turbs;
    FarmStatus m_st;

public:
    WindFarmClusterControl() = default;

    void add(int n = 4) {
        for (int i = 0; i < n; i++) m_turbs.push_back(new WindTurbineController(i + 1));
        m_st.count = n;
    }

    void setWind(double ws) { for (auto t : m_turbs) t->setWind(ws); }

    void distribute(double total, double dt, bool rise) {
        double each = total / m_turbs.size();
        for (auto t : m_turbs) { t->setTarget(each); t->execute(dt, rise); }
        update();
    }

    void preFluct(bool willRise) { for (auto t : m_turbs) t->preFluct(willRise); update(); }

    void update() {
        m_st = { 0,0,0,0,0,0,(int)m_turbs.size() };
        double p = 0, s = 0, act = 0, up = 0, down = 0;
        for (auto t : m_turbs) {
            auto st = t->status();
            act += st.actualPower;
            up += st.adjustMarginUp;
            down += st.adjustMarginDown;
            p += st.pitchAngle;
            s += st.rotorSpeed;
        }
        m_st.totalActual = act;
        m_st.totalUp = up;
        m_st.totalDown = down;
        m_st.avgPitch = p / m_turbs.size();
        m_st.avgSpeed = s / m_turbs.size();
    }

    // ? 平锟斤拷锟斤拷位锟斤拷锟斤拷瞬锟斤拷突锟斤拷
    void resetAllSmooth() { for (auto t : m_turbs) t->resetSmooth(); update(); }

    FarmStatus status() { return m_st; }
    int turbineCount() const { return (int)m_turbs.size(); }
    WindTurbineController* turbine(int i) { return (i >= 0 && i < (int)m_turbs.size()) ? m_turbs[i] : nullptr; }
    ~WindFarmClusterControl() { for (auto t : m_turbs) delete t; }
};
#endif