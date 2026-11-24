#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

class JsonWriter;

// Size of one ADC step in volts at hardware gain `gain`: the 8-bit scope samples
// span 1.65 V +/- 23.65 V / gain (o1buffer::sampleConvert, TOP = 128).
double scopeLsbVolts(double gain);

// Finds the times a signal rises through a level, with hysteresis: after a crossing
// the signal must drop below level - hysteresis before the next one counts, so
// noise around the level is not mistaken for edges. Fed sample by sample so a
// stream can be scanned in chunks; positions are fractional sample indices,
// linearly interpolated between the two samples either side of the level.
class RisingCrossings
{
  public:
    RisingCrossings(double level, double hysteresis) : m_level(level), m_hysteresis(hysteresis) {}

    // `index` is the sample's absolute position in the stream; calls must be in order.
    // Returns true and sets *position when this sample completes a crossing.
    bool feed(uint64_t index, double volts, double* position);

  private:
    double m_level;
    double m_hysteresis;
    bool m_armed = false;
    bool m_have_previous = false;
    double m_previous = 0;
};

// What `measure` reports for one channel's window of samples.
struct ChannelStats
{
    size_t samples = 0;
    double min_v = 0;
    double max_v = 0;
    double mean_v = 0;
    double rms_ac_v = 0; // standard deviation: the RMS of the signal less its mean
    double vpp = 0;

    // A window counts as periodic when it swings by at least MIN_PERIODIC_LSB ADC
    // steps and rises through its mid level at least twice. The rest is then valid.
    bool periodic = false;
    int cycles = 0;          // whole periods between the first and last rising crossing
    double frequency_hz = 0; // cycles / time from the first to the last crossing
    double period_min_s = 0; // shortest and longest single period: a glitch or a lost
    double period_max_s = 0; // USB frame shows up as an outlier here
    double duty = 0;         // fraction of samples above the mid level

    // Writes the stats as an object (the periodic fields are null when not periodic).
    void writeJson(JsonWriter& json) const;
};

constexpr double MIN_PERIODIC_LSB = 6;

// Mid level and hysteresis used for a signal swinging between `min_v` and
// `max_v`: halfway, with an eighth of the swing (but at least 1.5 ADC steps) of
// hysteresis.
void crossingLevels(double min_v, double max_v, double lsb_v, double* level, double* hysteresis);

ChannelStats analyseChannel(const std::vector<double>& volts, double sample_rate_hz, double lsb_v);
