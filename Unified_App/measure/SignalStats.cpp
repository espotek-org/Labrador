#include "SignalStats.h"

#include "JsonWriter.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

double scopeLsbVolts(double gain)
{
    // Full scale 1.65 V +/- (1.65 V / (75/1075)) / gain over +/-128 steps.
    return 1.65 / (75.0 / 1075.0) / gain / 128.0;
}

bool RisingCrossings::feed(uint64_t index, double volts, double* position)
{
    bool crossed = false;
    if (!m_armed)
    {
        m_armed = volts < m_level - m_hysteresis;
    }
    else if (volts >= m_level)
    {
        // Interpolate between the previous sample (below the level, as the
        // detector was armed) and this one.
        const double fraction =
            m_have_previous && volts != m_previous ? (m_level - m_previous) / (volts - m_previous) : 0.0;
        *position = static_cast<double>(index) - 1.0 + std::clamp(fraction, 0.0, 1.0);
        m_armed = false;
        crossed = true;
    }
    m_previous = volts;
    m_have_previous = true;
    return crossed;
}

void crossingLevels(double min_v, double max_v, double lsb_v, double* level, double* hysteresis)
{
    *level = (min_v + max_v) / 2.0;
    *hysteresis = std::max((max_v - min_v) / 8.0, 1.5 * lsb_v);
}

ChannelStats analyseChannel(const std::vector<double>& volts, double sample_rate_hz, double lsb_v)
{
    if (volts.empty())
        throw std::logic_error("analyseChannel: no samples");
    ChannelStats stats;
    stats.samples = volts.size();
    const auto [lo, hi] = std::minmax_element(volts.begin(), volts.end());
    stats.min_v = *lo;
    stats.max_v = *hi;
    stats.vpp = stats.max_v - stats.min_v;

    double sum = 0;
    for (const double v : volts)
        sum += v;
    stats.mean_v = sum / static_cast<double>(volts.size());
    double squares = 0;
    for (const double v : volts)
        squares += (v - stats.mean_v) * (v - stats.mean_v);
    stats.rms_ac_v = std::sqrt(squares / static_cast<double>(volts.size()));

    if (stats.vpp < MIN_PERIODIC_LSB * lsb_v)
        return stats;

    double level, hysteresis;
    crossingLevels(stats.min_v, stats.max_v, lsb_v, &level, &hysteresis);
    RisingCrossings detector(level, hysteresis);
    std::vector<double> crossings;
    size_t above = 0;
    for (size_t i = 0; i < volts.size(); i++)
    {
        double position;
        if (detector.feed(i, volts[i], &position))
            crossings.push_back(position);
        if (volts[i] > level)
            above++;
    }
    if (crossings.size() < 2)
        return stats;

    stats.periodic = true;
    stats.cycles = static_cast<int>(crossings.size()) - 1;
    stats.frequency_hz =
        stats.cycles * sample_rate_hz / (crossings.back() - crossings.front());
    double shortest = crossings[1] - crossings[0];
    double longest = shortest;
    for (size_t i = 2; i < crossings.size(); i++)
    {
        shortest = std::min(shortest, crossings[i] - crossings[i - 1]);
        longest = std::max(longest, crossings[i] - crossings[i - 1]);
    }
    stats.period_min_s = shortest / sample_rate_hz;
    stats.period_max_s = longest / sample_rate_hz;
    stats.duty = static_cast<double>(above) / static_cast<double>(volts.size());
    return stats;
}

void ChannelStats::writeJson(JsonWriter& json) const
{
    json.beginObject();
    json.field("samples", static_cast<uint64_t>(samples));
    json.field("min_v", min_v);
    json.field("max_v", max_v);
    json.field("mean_v", mean_v);
    json.field("rms_ac_v", rms_ac_v);
    json.field("vpp", vpp);
    json.field("periodic", periodic);
    const char* periodic_fields[] = {"cycles", "frequency_hz", "period_min_s", "period_max_s",
                                     "duty"};
    if (periodic)
    {
        json.field("cycles", cycles);
        json.field("frequency_hz", frequency_hz);
        json.field("period_min_s", period_min_s);
        json.field("period_max_s", period_max_s);
        json.field("duty", duty);
    }
    else
        for (const char* name : periodic_fields)
            json.key(name).null();
    json.endObject();
}
