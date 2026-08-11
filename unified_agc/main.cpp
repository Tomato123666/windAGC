/**
 * Wind Farm AGC - Daemon Mode
 *
 * Reads input signals from shared memory → runs AGC → writes results back.
 * Designed to work with HIL runner (tools/data_generator/runner.cpp).
 *
 * Usage:
 *   unified_agc.exe          (requires rt_db_init.exe running)
 *   Ctrl+C to stop
 */

#include "UnifiedAGC.h"
#include <cstdio>
#include <csignal>
#include <thread>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#endif

extern "C" {
#include "rt_db_api.h"
}

static volatile bool g_running = true;

#ifdef _WIN32
BOOL WINAPI onCtrlC(DWORD sig) {
    (void)sig;
    g_running = false;
    return TRUE;
}
#endif

// ============================================================
int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCtrlHandler(onCtrlC, TRUE);
#endif

    printf("============================================================\n");
    printf("  Wind Farm AGC - Daemon Mode\n");
    printf("  Reads  from SM: GRID.Frequency, WIND_AGC.*, COMM.*, etc.\n");
    printf("  Writes to   SM: WIND_AGC.TotalPower, .Setpoint, .Mode\n");
    printf("  Ctrl+C to stop\n");
    printf("============================================================\n\n");

    // ---- Init AGC ----
    unified::UnifiedAGC::Config cfg;
    cfg.turbineCount   = 10;
    cfg.turbineRatedMW = 3.0;
    cfg.totalRatedMW   = 30.0;

    unified::UnifiedAGC agc(cfg);
    agc.initialize();

    // ---- Connect output logger (writes farm state to SM) ----
    unified::UnifiedLogger logger;
    if (logger.initialize()) {
        agc.setLogger(&logger);
        printf("[OUT] Logger connected\n");
    } else {
        printf("[OUT] Logger not connected - run rt_db_init.exe first\n");
    }

    // ---- Connect input reader (reads external signals from SM) ----
    rt_db_handle_t inDb{};
    bool inOk = rt_db_init(&inDb, nullptr);

    size_t idxFreq    = (size_t)-1;
    size_t idxWind    = (size_t)-1;
    size_t idxComm    = (size_t)-1;
    size_t idxExtreme = (size_t)-1;
    size_t idxCurtail = (size_t)-1;
    size_t idxSched   = (size_t)-1;

    if (inOk) {
        idxFreq    = rt_db_find_index_by_id(&inDb, "GRID.Frequency");
        idxWind    = rt_db_find_index_by_id(&inDb, "WIND_AGC.WindSpeed");
        idxComm    = rt_db_find_index_by_id(&inDb, "COMM.IsHealthy");
        idxExtreme = rt_db_find_index_by_id(&inDb, "EXTREME.SubType");
        idxCurtail = rt_db_find_index_by_id(&inDb, "CURTAIL.Ratio");
        idxSched   = rt_db_find_index_by_id(&inDb, "WIND_AGC.SchedulePower");
        printf("[IN]  Input reader connected\n");
    } else {
        printf("[IN]  Input reader not connected\n");
    }

    // ---- Main loop ----
    const double DT_SEC = 1.0;
    int cycle = 0, lastPrintedScene = -1;
    double val;

    // Track previous state to avoid spam from unchanged inputs
    bool   prevCommHealthy = true;
    int    prevExtremeType = 0;
    double prevCurtailRatio = 0.0;
    double prevFrequencyHz = 50.0;
    double prevWindSpeedMs = 12.0;
    double prevScheduleMW = 15.0;

    printf("\n%-8s %-6s %-10s %-10s %-8s %-6s\n",
           "Cycle", "Scene", "TargetMW", "ActualMW", "Freq", "Wind");
    printf("-------- ------ ---------- ---------- -------- ------\n");

    while (g_running) {
        // --- Build dispatch command from SM inputs ---
        DispatchCommand cmd;
        cmd.targetPowerMW = prevScheduleMW;
        cmd.commandType   = CommandType::STEP;

        if (inOk) {
            // Grid frequency
            if (idxFreq != (size_t)-1 &&
                rt_db_get_value(&inDb, idxFreq, &val, nullptr, nullptr)) {
                if (val >= 49.0 && val <= 51.0 && val != prevFrequencyHz) {
                    agc.farm().frequencyHz = val;
                    prevFrequencyHz = val;
                }
            }

            // Wind speed
            if (idxWind != (size_t)-1 &&
                rt_db_get_value(&inDb, idxWind, &val, nullptr, nullptr)) {
                if (val > 0 && val != prevWindSpeedMs) {
                    agc.setWindSpeed(val);
                    prevWindSpeedMs = val;
                }
            }

            // Communication health (only call on change)
            if (idxComm != (size_t)-1 &&
                rt_db_get_value(&inDb, idxComm, &val, nullptr, nullptr)) {
                bool ok = (val > 0.5);
                if (ok != prevCommHealthy) {
                    agc.setCommHealthy(ok);
                    prevCommHealthy = ok;
                }
            }

            // Extreme weather (only call on change)
            if (idxExtreme != (size_t)-1 &&
                rt_db_get_value(&inDb, idxExtreme, &val, nullptr, nullptr)) {
                int et = (int)val;
                if (et != prevExtremeType) {
                    if (et > 0) {
                        agc.setExtremeWeather((ExtremeSubType)et,
                                              agc.farm().avgWindSpeedMs,
                                              agc.farm().avgTurbulence);
                    } else {
                        agc.clearExtremeWeather();
                    }
                    prevExtremeType = et;
                }
            }

            // Curtailment ratio (only on change)
            if (idxCurtail != (size_t)-1 &&
                rt_db_get_value(&inDb, idxCurtail, &val, nullptr, nullptr)) {
                if (val != prevCurtailRatio) {
                    agc.setCurtailRatio(val);
                    prevCurtailRatio = val;
                }
            }

            // Schedule / dispatch target (only on change)
            if (idxSched != (size_t)-1 &&
                rt_db_get_value(&inDb, idxSched, &val, nullptr, nullptr)) {
                if (val > 0.1 && val != prevScheduleMW) {
                    cmd.targetPowerMW = val;
                    prevScheduleMW = val;
                }
            }
        }

        // --- Run AGC step (includes logging to SM) ---
        agc.step(cmd, DT_SEC);

        cycle++;
        int curScene = agc.activeScene();

        // Print on scene change OR every 100 cycles
        if (curScene != lastPrintedScene || cycle % 100 == 0) {
            auto& f = agc.farm();
            printf("%-8d %-6d %-10.1f %-10.1f %-8.2f %-6.1f [%s]\n",
                   cycle, curScene,
                   f.targetPowerMW, f.totalPowerMW, f.frequencyHz,
                   f.avgWindSpeedMs, agc.strategyName());
            lastPrintedScene = curScene;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    printf("\n=== Daemon stopped. %d cycles, final scene %d ===\n",
           cycle, agc.activeScene());

    if (inOk) rt_db_cleanup(&inDb);
    return 0;
}