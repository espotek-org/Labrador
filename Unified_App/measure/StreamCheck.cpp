#include "StreamCheck.h"

#include "JsonWriter.h"
#include "SignalStats.h"

#include "librador.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <thread>

namespace
{
constexpr size_t MAX_LISTED_ANOMALIES = 20;
constexpr double LEVEL_WINDOW_S = 0.2; // samples used to pick the crossing level
constexpr double PROGRESS_INTERVAL_S = 5.0;

struct Follower
{
    int channel = 0;
    double vpp = 0;
    double level_v = 0;
    std::unique_ptr<RisingCrossings> detector;
    double last_crossing = -1;     // fractional sample index
    std::vector<double> periods;   // in samples
    std::vector<double> period_at; // sample index where each period ended

    void feed(uint64_t first, const std::vector<double>& volts)
    {
        for (size_t i = 0; i < volts.size(); i++)
        {
            double position;
            if (!detector->feed(first + i, volts[i], &position))
                continue;
            if (last_crossing >= 0)
            {
                periods.push_back(position - last_crossing);
                period_at.push_back(position);
            }
            last_crossing = position;
        }
    }

    StreamContinuity evaluate(uint64_t start_count, double sample_rate) const
    {
        StreamContinuity c;
        c.channel = channel;
        c.vpp = vpp;
        c.level_v = level_v;
        c.periods = periods.size();
        if (periods.empty())
            return c;
        std::vector<double> sorted = periods;
        std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
        c.median_period_samples = sorted[sorted.size() / 2];
        c.frequency_hz = sample_rate / c.median_period_samples;
        for (size_t i = 0; i < periods.size(); i++)
        {
            const double deviation =
                std::fabs(periods[i] - c.median_period_samples) / c.median_period_samples;
            c.max_period_deviation = std::max(c.max_period_deviation, deviation);
            if (deviation > STREAM_ANOMALY_FRACTION)
            {
                c.anomalies++;
                c.anomaly_at_s.push_back((period_at[i] - static_cast<double>(start_count)) / sample_rate);
                if (c.anomaly_times_s.size() < MAX_LISTED_ANOMALIES)
                    c.anomaly_times_s.push_back((period_at[i] - static_cast<double>(start_count)) /
                                                sample_rate);
            }
        }
        return c;
    }
};

void copyChannel(int channel, uint64_t start, uint64_t end, std::vector<double>& out)
{
    out.resize(static_cast<size_t>(end - start));
    const int rc = librador_copy_analog_window(channel, start, static_cast<int>(end - start),
                                               out.data());
    if (rc != 0)
        throw std::runtime_error("copying CH" + std::to_string(channel) +
                                 "'s samples failed (code " + std::to_string(rc) +
                                 "); the stream may have fallen behind");
}
} // namespace

StreamCheckResult runStreamCheck(const DeviceSession& session, double seconds, const bool check[2],
                                 double gain,
                                 const std::function<void(const std::string&)>& progress)
{
    const double lsb = scopeLsbVolts(gain);

    // Pick each checked channel's crossing level from a short first window.
    std::vector<Follower> followers;
    const SampleWindow first = session.acquire(LEVEL_WINDOW_S);
    for (int ch = 1; ch <= 2; ch++)
    {
        if (!check[ch - 1])
            continue;
        const std::vector<double>& volts = ch == 1 ? first.ch1 : first.ch2;
        if (volts.empty())
            throw std::runtime_error("CH" + std::to_string(ch) + " does not stream in this mode");
        const ChannelStats stats = analyseChannel(volts, first.sample_rate_hz, lsb);
        if (!stats.periodic)
        {
            char message[160];
            std::snprintf(message, sizeof message,
                          "CH%d carries no periodic signal to follow (it swings %.3g V); is a "
                          "generator wired to it and driven?",
                          ch, stats.vpp);
            throw std::runtime_error(message);
        }
        Follower f;
        f.channel = ch;
        f.vpp = stats.vpp;
        double hysteresis;
        crossingLevels(stats.min_v, stats.max_v, lsb, &f.level_v, &hysteresis);
        f.detector = std::make_unique<RisingCrossings>(f.level_v, hysteresis);
        followers.push_back(std::move(f));
    }

    using clock = std::chrono::steady_clock;
    StreamCheckResult result;
    result.sample_rate_hz = librador_get_samples_per_second();
    uint64_t scan;
    if (librador_get_analog_sample_count(1, &scan) != 0)
        throw std::runtime_error("CH1 is not streaming");
    const uint64_t start_count = scan;
    const FrameStats frames_start = FrameStats::now();
    const auto t_start = clock::now();
    auto last_progress = t_start;
    auto last_growth = t_start;
    std::vector<double> chunk;

    while (true)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        const auto now = clock::now();
        uint64_t count;
        if (librador_get_analog_sample_count(1, &count) != 0 || count < scan)
            throw std::runtime_error("the sample stream restarted during the test");
        if (count > scan)
        {
            for (Follower& f : followers)
            {
                copyChannel(f.channel, scan, count, chunk);
                f.feed(scan, chunk);
            }
            scan = count;
            last_growth = now;
        }
        result.max_stall_s = std::max(result.max_stall_s,
                                      std::chrono::duration<double>(now - last_growth).count());

        const double elapsed = std::chrono::duration<double>(now - t_start).count();
        if (std::chrono::duration<double>(now - last_progress).count() >= PROGRESS_INTERVAL_S)
        {
            last_progress = now;
            const FrameStats f = FrameStats::now().since(frames_start);
            char line[240];
            int n = std::snprintf(line, sizeof line,
                                  "%.0f of %.0f s, %llu samples, frames %llu ok, %llu bad checksum, "
                                  "%llu dropped, %llu unvalidated",
                                  elapsed, seconds, static_cast<unsigned long long>(scan - start_count),
                                  static_cast<unsigned long long>(f.ok),
                                  static_cast<unsigned long long>(f.bad_checksum),
                                  static_cast<unsigned long long>(f.dropped),
                                  static_cast<unsigned long long>(f.unvalidated));
            for (const Follower& fl : followers)
                n += std::snprintf(line + n, sizeof line - n, ", CH%d period anomalies %llu",
                                   fl.channel,
                                   static_cast<unsigned long long>(
                                       fl.evaluate(start_count, result.sample_rate_hz).anomalies));
            progress(line);
        }
        if (elapsed >= seconds)
            break;
    }
    result.elapsed_s = std::chrono::duration<double>(last_growth - t_start).count();
    result.frames = FrameStats::now().since(frames_start);
    result.samples = scan - start_count;
    result.delivered_sample_rate_hz =
        result.elapsed_s > 0 ? static_cast<double>(result.samples) / result.elapsed_s : 0;
    for (const Follower& f : followers)
        result.continuity.push_back(f.evaluate(start_count, result.sample_rate_hz));
    return result;
}

void StreamCheckResult::writeJson(JsonWriter& json) const
{
    json.field("sample_rate_hz", sample_rate_hz);
    json.field("samples", samples);
    json.field("delivered_sample_rate_hz", delivered_sample_rate_hz);
    json.field("max_stall_s", max_stall_s);
    json.key("frames");
    frames.writeJson(json);
    json.field("anomaly_fraction", STREAM_ANOMALY_FRACTION);
    json.key("continuity").beginArray();
    for (const StreamContinuity& c : continuity)
    {
        json.beginObject();
        json.field("channel", c.channel);
        json.field("vpp", c.vpp);
        json.field("level_v", c.level_v);
        json.field("periods", c.periods);
        json.field("median_period_samples", c.median_period_samples);
        json.field("frequency_hz", c.frequency_hz);
        json.field("max_period_deviation", c.max_period_deviation);
        json.field("anomalies", c.anomalies);
        json.key("anomaly_times_s").beginArray();
        for (const double t : c.anomaly_times_s)
            json.value(t);
        json.endArray();
        json.endObject();
    }
    json.endArray();
}
