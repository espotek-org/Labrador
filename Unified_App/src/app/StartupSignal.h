#pragma once

#include <optional>

// Testing hook, like LABRADOR_LAYOUT: LABRADOR_SG1 / LABRADOR_SG2 start the app
// with that signal generator already switched on, so a headless screenshot of a
// board with its generators looped back to the scope shows a live trace. The
// value uses labrador-cli's --sg1 syntax for periodic waves,
// WAVE:FREQ:VPP[:BASE] with WAVE sine, square, triangle or sawtooth (see
// cli/README.md). The app's own limits still apply once it runs (the PSU-follows
// clamp keeps the peak 1.18 V under the PSU voltage).
struct StartupSignal
{
    enum class Wave
    {
        Sine,
        Square,
        Triangle,
        Sawtooth
    };
    Wave wave;
    float frequency_hz;
    float vpp;
    float base_v;
};

// The signal LABRADOR_SG<channel> asks for, or nothing when the variable is
// unset. Throws std::runtime_error when it is set but malformed, so a typo in
// a test run fails at startup instead of silently showing a dark scope.
std::optional<StartupSignal> startupSignalOrNone(int channel);
