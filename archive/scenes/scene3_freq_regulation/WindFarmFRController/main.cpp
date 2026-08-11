#include "FRController.h"
#include "RtDbLogger.h"
#ifdef _WIN32
#include <windows.h>
#endif
int main()
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    // Initialize shared memory
    RtDbLogger rtDb;
    if (rtDb.initialize()) {
        std::cout << "[Shared Memory] Connected, FR data streaming to RT_DB" << std::endl;
    }

    FRController ctrl;
    ctrl.setRtDbLogger(&rtDb);
    ctrl.start();

    printLog("[INFO] Wind farm FR system started (10 x 2.5MW)");
    printLog("[INFO] Freq deadband: +/-0.10Hz | FR capacity: +/-1500kW");

    std::this_thread::sleep_for(std::chrono::seconds(2));
    printLog("[INFO] Triggering frequency disturbance -> 49.7Hz");
    ctrl.triggerDisturbance(49.7);

    // Keep main thread alive
    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }

    return 0;
}
