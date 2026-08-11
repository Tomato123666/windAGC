#ifndef FR_CONTROLLER_H
#define FR_CONTROLLER_H

#include "FR_Types.h"
#include "FrequencySimulator.h"
#include "StateMachine.h"
#include "PowerCalculator.h"
#include "Allocator.h"
#include "RtDbLogger.h"
#include <iomanip>

class FRController
{
public:
    FrequencySimulator freqSim;
    StateMachine sm;
    PowerCalculator calc;
    Allocator alloc;
    std::vector<Turbine> turbines;
    bool running = false;
    FRState lastState = READY;
    RtDbLogger* rtDb = nullptr;

    FRController()
    {
        for (int i = 1; i <= 10; ++i)
        {
            Turbine t;
            t.id = i;
            turbines.push_back(t);
        }
    }

    void setRtDbLogger(RtDbLogger* logger) { rtDb = logger; }

    void start()
    {
        running = true;
        std::thread(&FRController::runLoop, this).detach();
    }

    void triggerDisturbance(double f)
    {
        freqSim.triggerDisturbance(f);
        if (rtDb) rtDb->logDisturbance(f);
    }

private:
    void runLoop()
    {
        double frPower = 0.0;

        while (running)
        {
            double freq = freqSim.getFrequency(frPower);
            double deltaF = freq - 50.0;
            FRState state = sm.update(deltaF);

            // State change logging
            if (state != lastState)
            {
                if (state == ACTIVE)
                    printLog("[EVENT] Frequency limit exceeded, starting FR");
                else if (state == RECOVERY)
                    printLog("[EVENT] Frequency recovering");
                else if (state == READY)
                {
                    printLog("[EVENT] FR complete, frequency stable at 50.00Hz");
                    printLog("[INFO] System back to READY, waiting for next disturbance");
                    calc.reset();
                    freqSim.reset();
                }
                lastState = state;
            }

            // Calculate FR power
            double df_dt = deltaF / 0.1;
            frPower = calc.calculate(deltaF, df_dt);

            // Allocate to turbines
            auto cmds = alloc.allocate(frPower, turbines);

            // Write to shared memory
            if (rtDb && state != READY) {
                rtDb->logFRCycle(freq, deltaF, df_dt, (int)state, frPower,
                                 -30.0 * deltaF,
                                 -0.8 * calc.getIntegral());
                for (const auto& c : cmds) {
                    rtDb->logTurbineFR(c.turbineId, c.basePower,
                                       c.frPower, c.finalPower, true);
                }
            }

            if (state == READY)
            {
                std::cout << "\r>>> Frequency stable: 50.00Hz, system running normally..." << std::flush;
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }
            std::cout << "\n==================================================" << std::endl;
            std::cout << "Grid Freq: " << fmt(freq) << " Hz | Delta: " << fmt(deltaF) << " Hz | State: " << stateToStr(state) << std::endl;
            std::cout << "Total FR Power: " << fmt(frPower) << " kW" << std::endl;
            std::cout << "==================================================" << std::endl;

            std::cout << std::left
                << std::setw(8) << "Turbine"
                << std::setw(12) << "Base(kW)"
                << std::setw(12) << "FR(kW)"
                << std::setw(12) << "Cmd(kW)" << std::endl;

            for (const auto& c : cmds)
            {
                std::cout << std::left
                    << std::setw(8) << c.turbineId
                    << std::setw(12) << fmt(c.basePower)
                    << std::setw(12) << fmt(c.frPower)
                    << std::setw(12) << fmt(c.finalPower) << std::endl;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(400));
        }
    }

    std::string stateToStr(FRState s) const
    {
        switch (s)
        {
        case READY: return "READY";
        case ACTIVE: return "ACTIVE";
        case RECOVERY: return "RECOVERY";
        case FAULT: return "FAULT";
        default: return "?";
        }
    }
};

#endif
