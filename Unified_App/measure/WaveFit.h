#pragma once

#include "Signal.h"

#include <vector>

// The generator's waveform over one period, 0 (low) to 1 (high), at `phase` in
// cycles. Matches librador's generator_* tables (librador.cpp): sine (1 + sin)/2,
// square low for the first half, sawtooth a rising ramp, triangle up then down.
double idealShape(SignalSpec::Wave wave, double phase);

// The ideal waveform fitted to captured samples by least squares.
struct WaveFit
{
    double low_v = 0;  // fitted level of the shape's 0
    double high_v = 0; // fitted level of the shape's 1
    double frequency_hz = 0;
    double phase = 0;  // cycles, at t = 0
    double residual_rms_v = 0;
    double residual_fraction = 0; // residual RMS / (high - low)

    double vpp() const { return high_v - low_v; }
    double at(SignalSpec::Wave wave, double t) const;
};

// Fits low/high levels, phase and frequency of `wave` to samples `y` taken at
// `sample_rate_hz` (sample i at t = (i - zero_index) / rate), the frequency
// within `frequency_span` (a fraction) of `frequency_hz`.
//
// The frequency floats because a generator runs a few hundred ppm off its
// nominal frequency (judged on its own by the frequency checks); over the
// thousand periods of a 20 kHz window that much drift would otherwise read as
// a wrong shape. Phase is fitted at the window's middle, where a small
// frequency error moves it least: a coarse search at the nominal frequency on at
// most 20000 evenly spread samples, a joint search of frequency and fine phase,
// then the levels and residual from all samples. Throws std::runtime_error when
// no upright shape fits (fewer than 10 samples, or a flat signal).
WaveFit fitWave(SignalSpec::Wave wave, double frequency_hz, double frequency_span,
                const std::vector<double>& y, double sample_rate_hz, double zero_index);
