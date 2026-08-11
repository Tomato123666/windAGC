#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include "FR_Types.h"

class StateMachine
{
private:
    FRState state = READY;
    const double deadband = 0.10;
    const double recoverThresh = 0.02;
    const double readyThresh = 0.003;

public:
    FRState update(double deltaF)
    {
        switch (state)
        {
        case READY:
            if (fabs(deltaF) > deadband)
                state = ACTIVE;
            break;
        case ACTIVE:
            if (fabs(deltaF) < recoverThresh)
                state = RECOVERY;
            break;
        case RECOVERY:
            if (fabs(deltaF) < readyThresh)
                state = READY;
            break;
        case FAULT:
            break;
        }
        return state;
    }

    FRState getState() const { return state; }
};

#endif