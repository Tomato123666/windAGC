#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include "FR_Types.h"
#include <algorithm>

// Power allocation: evenly distribute total FR power across running, non-faulted turbines
class Allocator
{
public:
    std::vector<FRCommand> allocate(double totalFr, const std::vector<Turbine>& turbines) const
    {
        std::vector<FRCommand> res;
        int count = 0;

        for (const auto& t : turbines)
            if (t.running && !t.fault) count++;

        if (count == 0) return res;

        double each = totalFr / count;

        for (const auto& t : turbines)
        {
            if (!t.running || t.fault) continue;

            FRCommand cmd;
            cmd.turbineId = t.id;
            cmd.basePower = t.power;
            cmd.frPower = each;
            cmd.finalPower = t.power + each;

            // Clamp to turbine physical limits
            cmd.finalPower = std::max(cmd.finalPower, t.minPower);
            cmd.finalPower = std::min(cmd.finalPower, t.maxPower);

            res.push_back(cmd);
        }

        return res;
    }
};

#endif
