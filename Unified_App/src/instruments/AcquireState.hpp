#pragma once
#include <chrono>
struct SpectrumAcquireState {
    bool acquiring = false;
    float  max_time_s = 1.0f;
    double elapsed_last_s = 0.0;
    std::chrono::steady_clock::time_point t0{};
    double success_until_time = 0.0;
};

struct NetworkAcquireState {
    bool acquiring = false;
    double t_start_s = 0.0;
    double elapsed_last_s = 0.0;
    double success_until_time = 0.0;
};