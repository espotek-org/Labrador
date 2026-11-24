#pragma once

#include <string>

class JsonWriter;

// One signal generator setting: off, a DC level, or a periodic wave described
// the way the app's Signals page does (Vpeak-peak above a Vbase).
struct SignalSpec
{
    enum class Wave
    {
        Off,
        Dc,
        Sine,
        Square,
        Triangle,
        Sawtooth
    };
    Wave wave = Wave::Off;
    double frequency_hz = 0; // periodic waves only
    double vpp = 0;          // periodic waves only
    double base_v = 0;       // periodic waves: the lowest voltage; dc: the level

    static SignalSpec off() { return {}; }
    static SignalSpec dc(double volts) { return {Wave::Dc, 0, 0, volts}; }
    static SignalSpec periodic(Wave wave, double frequency_hz, double vpp, double base_v)
    {
        return {wave, frequency_hz, vpp, base_v};
    }

    bool isPeriodic() const { return wave != Wave::Off && wave != Wave::Dc; }
    // Highest voltage asked for (0 when off).
    double peakV() const;
    // Canonical spelling, e.g. "sine:1000:2:0.5" (labrador-cli's --sg1 syntax).
    std::string text() const;
};

// "sine", "square", ... for a wave (and "off", "dc").
const char* waveName(SignalSpec::Wave wave);

// What a generator actually outputs once librador has rounded the request to the
// timer period and the 8-bit DAC (librador_get_signal_gen_output).
struct SignalOutput
{
    double frequency_hz = 0; // 0 for off and dc
    double v_min = 0;
    double v_max = 0;
};

// The power supply rail also feeds the generators' output stage, which needs
// this much headroom: a signal's peak may be at most the PSU voltage minus this.
// Same limit as the app (SGControl's PSU-follows clamp).
constexpr double SIGGEN_PSU_HEADROOM_V = 1.18;
constexpr double PSU_MIN_V = 4.5;
constexpr double PSU_MAX_V = 11.0;
// The generators' range (SignalType.hpp: 0.15-9 Vpp and 0-9 V base; librador's
// 9.6 V output limit). 100 kHz still leaves the DAC (1 MS/s, at most 512
// samples a period) ten samples per period.
constexpr double SIGNAL_MIN_FREQUENCY_HZ = 1;
constexpr double SIGNAL_MAX_FREQUENCY_HZ = 100000;
constexpr double SIGNAL_MIN_VPP = 0.15;
constexpr double SIGNAL_MAX_VPP = 9.0;
constexpr double SIGNAL_MAX_BASE_V = 9.0;
constexpr double SIGNAL_MAX_PEAK_V = 9.6;

// Over a connected board: set the PSU (throws std::runtime_error if refused).
void applyPsu(double volts);

// Sends `spec` to generator `channel` (1 = SG1, 2 = SG2) and returns what the
// hardware was set to output. Throws std::runtime_error if the board refuses.
SignalOutput applySignal(int channel, const SignalSpec& spec);

// {"spec": ..., "wave": ..., [frequency_hz, vpp,] [base_v,] "actual": {...}}
void writeSignalJson(JsonWriter& json, const SignalSpec& spec, const SignalOutput& out);
