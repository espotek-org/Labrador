#pragma once

#include "DeviceSession.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class JsonWriter;

// A period counts as an anomaly when it differs from the median period by more
// than this fraction. One lost or repeated 1 ms USB frame shifts the waveform by
// 375 samples, which exceeds this for any period under 7500 samples (above 50 Hz).
constexpr double STREAM_ANOMALY_FRACTION = 0.05;

// Continuity of one input's periodic signal over a stream.
struct StreamContinuity
{
    int channel = 0;
    double vpp = 0;
    double level_v = 0;
    uint64_t periods = 0;
    double median_period_samples = 0;
    double frequency_hz = 0; // from the median period
    double max_period_deviation = 0; // fraction of the median
    uint64_t anomalies = 0;
    std::vector<double> anomaly_times_s; // the first 20, from the start of the stream
    std::vector<double> anomaly_at_s;    // every one (not written to JSON)
};

struct StreamCheckResult
{
    double sample_rate_hz = 0; // nominal
    double elapsed_s = 0;
    uint64_t samples = 0;
    double delivered_sample_rate_hz = 0;
    double max_stall_s = 0; // longest gap between sample arrivals
    FrameStats frames;
    std::vector<StreamContinuity> continuity;

    // The result's members (sample_rate_hz ... continuity) into the current object.
    void writeJson(JsonWriter& json) const;
};

// Streams the configured scope for `seconds` and counts what the USB link lost,
// following the periodic signal on each input marked in `check` (index 0 = CH1)
// and flagging periods more than STREAM_ANOMALY_FRACTION off the median. `gain`
// is the scope gain in use (for the ADC step). `progress` gets a one-line summary
// about every five seconds. Throws std::runtime_error if a checked input carries
// no periodic signal or the stream restarts.
StreamCheckResult runStreamCheck(const DeviceSession& session, double seconds, const bool check[2],
                                 double gain,
                                 const std::function<void(const std::string&)>& progress);
