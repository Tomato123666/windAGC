#ifndef AGC_H
#define AGC_H
#include "WindFarmClusterControl.h"
#include <string>

enum AGCMode { ECO, SUPPRESS };

class AGC {
private:
    AGCMode _mode;
    WindFarmClusterControl* _farm;
    const double GRID_TARGET = 10.0;

public:
    AGC(WindFarmClusterControl* f) : _farm(f), _mode(ECO) {}

    void enterSuppress(bool willRise) {
        _mode = SUPPRESS;
        _farm->preFluct(willRise);
    }

    void run(double dt, bool isRise) {
        _farm->distribute(GRID_TARGET, dt, isRise);
    }

    void backEco() {
        _mode = ECO;
    }

    std::string modeStr() {
        return _mode == ECO ? "Economic Mode" : "Fluctuation Suppression Mode";
    }

    AGCMode mode() { return _mode; }
    double target() { return GRID_TARGET; }
};
#endif