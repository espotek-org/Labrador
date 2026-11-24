#include "StreamTestCommand.h"

#include "DeviceSession.h"
#include "JsonWriter.h"
#include "OptionParser.h"
#include "ScopeOptions.h"
#include "StreamCheck.h"
#include "Stimulus.h"

#include <cstdio>
#include <iostream>

namespace
{
struct Options
{
    ScopeOptions scope;
    Stimulus stimulus;
    double seconds = 60;
    bool check[2] = {false, false}; // CH1, CH2
    bool json = false;
};

OptionParser makeParser(Options& options)
{
    const Options defaults;
    OptionParser parser;
    parser.addOption("--seconds", "S",
                     "how long to stream; default " + OptionParser::formatNumber(defaults.seconds),
                     [&](const std::string& v)
                     { options.seconds = OptionParser::toNonNegative("--seconds", v); });
    parser.addOption("--check", "CH",
                     "ch1, ch2 or both: the channels carrying a periodic signal (a "
                     "generator wired to the input) whose continuity to check; default none",
                     [&](const std::string& v)
                     {
                         if (v == "ch1")
                             options.check[0] = true;
                         else if (v == "ch2")
                             options.check[1] = true;
                         else if (v == "both")
                             options.check[0] = options.check[1] = true;
                         else
                             throw UsageError("option '--check' must be ch1, ch2 or both, got '" +
                                              v + "'");
                     });
    options.stimulus.addOptions(parser);
    options.scope.addOptions(parser);
    parser.addFlag("--json", "print the results as JSON instead of text",
                   [&]() { options.json = true; });
    return parser;
}
} // namespace

std::string StreamTestCommand::name() const
{
    return "stream-test";
}

std::string StreamTestCommand::summary() const
{
    return "Stream the scope for a while and check no USB frames are lost or corrupt";
}

void StreamTestCommand::printHelp(std::ostream& out, const std::string& program) const
{
    Options unused;
    out << program << " stream-test - " << summary() << "\n\n"
        << "Streams the scope for --seconds and reports the USB frame counters (frames\n"
        << "with a bad checksum, frames dropped in transit, frames whose checksum record\n"
        << "never arrived), the sample rate actually delivered, and the longest pause in\n"
        << "the stream. With --check, it also follows a periodic signal on that channel\n"
        << "(for example SG1 wired to CH1 and driven with --sg1) and counts periods more\n"
        << "than 5% off the median: a lost or repeated USB frame shifts the waveform and\n"
        << "shows up there even when no counter notices. Progress goes to stderr every\n"
        << "few seconds.\n\n"
        << "Usage:\n"
        << "  " << program << " stream-test [options]\n\n"
        << "Options:\n";
    makeParser(unused).printOptions(out);
    out << '\n'
        << signalSpecHelp() << '\n'
        << "Example (SG1 wired to CH1; 354.1 Hz does not line up with the 1 ms USB frame,\n"
        << "so every frame carries a different part of the waveform):\n"
        << "  " << program << " stream-test --sg1 sine:354.1:2:0.5 --check ch1 --seconds 120\n\n"
        << "Exit status is 0 whenever the test ran, whatever it found, so scripts must\n"
        << "read the counts to judge the link.\n";
}

int StreamTestCommand::run(const std::vector<std::string>& args)
{
    Options options;
    makeParser(options).parse(args);
    options.stimulus.validate();
    if (options.seconds <= 0)
        throw UsageError("option '--seconds' must be more than 0");
    if (options.check[1] && options.scope.mode != ScopeMode::Ch1Ch2)
        throw UsageError("checking ch2 needs '--mode scope2'");

    DeviceSession session(options.scope.verbose, options.scope.transport);
    SignalOutput out1, out2;
    options.stimulus.apply(out1, out2);
    session.configureScope(options.scope.mode, options.scope.gain, options.scope.settle_s);

    std::cerr << "stream-test: streaming for " << options.seconds << " s over "
              << transportName(session.activeTransport()) << std::endl;
    const StreamCheckResult r =
        runStreamCheck(session, options.seconds, options.check, options.scope.gain,
                       [](const std::string& line) { std::cerr << "stream-test: " << line << std::endl; });

    if (options.json)
    {
        JsonWriter json(std::cout);
        json.beginObject();
        json.field("command", "stream-test");
        session.writeJson(json);
        json.field("mode", scopeModeName(options.scope.mode));
        json.field("gain", options.scope.gain);
        json.field("seconds", options.seconds);
        json.key("stimulus").beginObject();
        options.stimulus.writeJson(json, out1, out2);
        json.endObject();
        r.writeJson(json);
        json.endObject();
        json.finish();
        return EXIT_SUCCESS;
    }

    char line[240];
    std::snprintf(line, sizeof line,
                  "streamed %.1f s over %s: %llu samples (%.1f S/s delivered, %.0f expected), "
                  "longest pause %.3f s\n",
                  r.elapsed_s, transportName(session.activeTransport()).c_str(),
                  static_cast<unsigned long long>(r.samples), r.delivered_sample_rate_hz,
                  r.sample_rate_hz, r.max_stall_s);
    std::cout << line;
    std::snprintf(line, sizeof line,
                  "USB frames: %llu ok, %llu bad checksum, %llu dropped, %llu unvalidated\n",
                  static_cast<unsigned long long>(r.frames.ok),
                  static_cast<unsigned long long>(r.frames.bad_checksum),
                  static_cast<unsigned long long>(r.frames.dropped),
                  static_cast<unsigned long long>(r.frames.unvalidated));
    std::cout << line;
    for (const StreamContinuity& c : r.continuity)
    {
        std::snprintf(line, sizeof line,
                      "CH%d: %llu periods of %.2f samples (%.6g Hz), largest deviation %.2f%%, "
                      "%llu more than %.0f%% off\n",
                      c.channel, static_cast<unsigned long long>(c.periods),
                      c.median_period_samples, c.frequency_hz, c.max_period_deviation * 100,
                      static_cast<unsigned long long>(c.anomalies), STREAM_ANOMALY_FRACTION * 100);
        std::cout << line;
    }
    std::cout.flush();
    return EXIT_SUCCESS;
}
