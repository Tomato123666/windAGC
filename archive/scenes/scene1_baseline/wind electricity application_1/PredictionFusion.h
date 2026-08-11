#pragma once
#include <functional>
#include "Utils.h"

using PredictionFunc = std::function<double(double)>;

class PredictionFusion {
public:
    PredictionFusion(PredictionFunc pred_func, double total_rated_power, double alpha_ff = 0.9, double max_ff_pu = 0.3);
    double computeFeedforward(double schedule_power, double current_time_sec);

private:
    PredictionFunc pred_func_;
    double total_rated_power_;
    double alpha_ff_;
    double max_ff_pu_;
    double confidence_;
    Utils::LowPassFilter lpf_;
};