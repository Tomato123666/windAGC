// ============================================================================
// command_checker.cpp
// ============================================================================
#include "command_checker.h"

double CommandChecker::checkAndClamp(double target_cmd, double total_max,
    double total_min, bool& capped)
{
    capped = false;
    if (target_cmd > total_max) {
        capped = true;
        return total_max;
    }
    if (target_cmd < total_min) {
        capped = true;
        return total_min;
    }
    return target_cmd;
}