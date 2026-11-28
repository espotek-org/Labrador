#pragma once

// Pass/fail limits for --auto-qa's hardware tests, each derived from measuring a
// real board (docs/auto-qa.md, "Tolerances", has the data). The reference board
// is uncalibrated (it stores no calibration and the QA never writes one), so the
// level limits cover an uncalibrated scope front end; frequency limits are tight
// because the generator's DAC timer and the scope's ADC run from the same clock.

#include <cmath>

namespace autoqa::tol
{
// One ADC step: the 8-bit scope samples span 1.65 V +/- 23.65 V / gain.
inline double lsbV(double gain)
{
    return 1.65 / (75.0 / 1075.0) / gain / 128.0;
}
inline double scopeLow(double gain)
{
    return 1.65 - 1.65 / (75.0 / 1075.0) / gain;
}
inline double scopeHigh(double gain)
{
    return 1.65 + 1.65 / (75.0 / 1075.0) / gain;
}

// How far a measured level (a DC reading, a fitted waveform low/high) may sit
// from the level the generator was set to. Measured, uncalibrated: offsets of
// -0.03 to -0.14 V near 0 V on both inputs, a CH1 gain error near +3 %, and up
// to 1.6 extra ADC steps at gains 0.5 and 1; worst 0.22 V at 0.5 V, 0.38 V at
// 6.5 V (gain 2), 0.40 V at 0.5 V (gain 0.5). 0.25 V + 3 % + 2 ADC steps is at
// least 1.5x every observed error.
inline double level(double volts, double gain)
{
    return 0.25 + 0.03 * std::fabs(volts) + 2.0 * lsbV(gain);
}

// Measured frequency vs librador's realised frequency: 0-300 ppm on SG1 and up
// to 420 ppm on SG2 (its DMA appears to lose a little time each buffer wrap).
constexpr double FREQUENCY = 0.001;
// librador rounds the DAC sample period to whole timer ticks of at least 48,
// so the realised frequency is within 0.5/48 = 1.04 % of the request.
constexpr double REALISED_FREQUENCY = 0.0105;

// Shape residual (RMS of samples minus the fitted ideal shape, frequency free
// within FREQUENCY, over the fitted swing). Worst on clean captures: sine 2.9 %
// (20 kHz; 1.4 % at 2 kHz), triangle 1.3 %, sawtooth 4.5 %, square 6.7 %
// (single-sample spikes at the edges); about twice that. Captures broken by lost
// USB frames measured 55-180 %. Sine and triangle residuals differ by as little
// as 1.3x, so the expected shape must also fit best of the four.
constexpr double SHAPE_SINE = 0.06;
constexpr double SHAPE_TRIANGLE = 0.03;
constexpr double SHAPE_SAWTOOTH = 0.09;
constexpr double SHAPE_SQUARE = 0.14;

// Square duty cycle from the mid-level crossings: measured 0.493-0.498.
constexpr double SQUARE_DUTY = 0.02;

// Noise floor with both generators parked (0.06 V), gain 4: the median AC RMS of 5 ms slices.
// Whole-window RMS on the reference board: CH1 12.6-18.1 mV over 36 runs; CH2 19.8-36.9 mV
// over 115 (median 25.0), lifted and spread by bursts it picks up from outside the board,
// which is why the limit applies to the median slice.
constexpr double NOISE_RMS_V = 0.030;

// Crosstalk: idle input's AC RMS over the driven input's, 6 Vpp at 2 kHz, gain 4.
// Square edges couple as single-sample spikes: 1.3-2.7 %; a sine 1.1-1.5 %.
constexpr double CROSSTALK_SQUARE = 0.05;
constexpr double CROSSTALK_SINE = 0.03;

// Channel gain (slope of DC readings vs set level): measured 0.968-1.03.
constexpr double CHANNEL_GAIN = 0.06;

// Stream: a healthy link loses nothing.
constexpr double SAMPLE_RATE = 0.005; // delivered vs nominal
constexpr double MAX_STALL_S = 0.25;  // bulk transfers land every 20-40 ms

// A waveform test's frequency must put a lost 1 ms USB frame at least this far from a whole
// number of periods, or the lost frame would not show: 2237 Hz (realised 2239.6 Hz) gives
// 0.24 of a period and 831 Hz (realised 829.6 Hz) 0.83, where 2 kHz (realised 1994.7 Hz)
// gave 0.995.
constexpr double LOST_FRAME_SHIFT_MIN = 0.1;

// One 8-bit DAC step below 3.2 V, three times that through the x3 stage.
inline double dacStepV(double peak_v)
{
    return 3.2 / 255 * (peak_v > 3.2 ? 3 : 1);
}
} // namespace autoqa::tol
