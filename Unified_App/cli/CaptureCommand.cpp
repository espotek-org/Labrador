#include "CaptureCommand.h"

#include "DeviceSession.h"
#include "JsonWriter.h"
#include "OptionParser.h"
#include "SampleCsv.h"
#include "ScopeOptions.h"
#include "Stimulus.h"

#include "librador.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

namespace
{
struct Options
{
    std::string out;
    ScopeOptions scope;
    // Applied only when --sg1, --sg2 or --psu is given, so capturing an external
    // circuit never disturbs what the board is driving.
    Stimulus stimulus;
    librador_capture_request request; // trigger, hysteresis, pre/post, timeout
};

void parseTrigger(const std::string& spec, librador_capture_request& request)
{
    std::vector<std::string> parts;
    std::istringstream in(spec);
    for (std::string part; std::getline(in, part, ':');)
        parts.push_back(part);

    if (parts.size() != 3 || (parts[0] != "ch1" && parts[0] != "ch2") ||
        (parts[1] != "rising" && parts[1] != "falling"))
        throw UsageError("option '--trigger' expects CH:EDGE:VOLTS such as "
                         "ch1:rising:3.0, got '" +
                         spec + "'");

    request.trigger_channel = parts[0] == "ch1" ? 1 : 2;
    request.rising = parts[1] == "rising";
    request.level_v = OptionParser::toDouble("--trigger", parts[2]);
}

OptionParser makeParser(Options& options)
{
    const Options defaults;
    const librador_capture_request& d = defaults.request;
    OptionParser parser;

    parser.addOption("--out", "FILE",
                     "CSV file to write (required); FILE.json gets the metadata",
                     [&](const std::string& v) { options.out = v; });
    parser.addOption("--trigger", "CH:EDGE:VOLTS",
                     "channel (ch1|ch2), edge (rising|falling), level; default "
                     "ch1:rising:" + OptionParser::formatNumber(d.level_v),
                     [&](const std::string& v) { parseTrigger(v, options.request); });
    parser.addOption("--hysteresis", "VOLTS",
                     "signal must leave the level by this much to re-arm; default " +
                         OptionParser::formatNumber(d.hysteresis_v),
                     [&](const std::string& v) {
                         options.request.hysteresis_v =
                             OptionParser::toNonNegative("--hysteresis", v);
                     });
    parser.addOption("--pre", "SECONDS",
                     "keep this long before the trigger; default " +
                         OptionParser::formatNumber(d.pre_s),
                     [&](const std::string& v)
                     { options.request.pre_s = OptionParser::toNonNegative("--pre", v); });
    parser.addOption("--post", "SECONDS",
                     "keep this long after the trigger; default " +
                         OptionParser::formatNumber(d.post_s),
                     [&](const std::string& v)
                     { options.request.post_s = OptionParser::toNonNegative("--post", v); });
    parser.addOption("--timeout", "SECONDS",
                     "give up if no trigger; 0 waits forever (default)",
                     [&](const std::string& v)
                     { options.request.timeout_s = OptionParser::toNonNegative("--timeout", v); });
    options.scope.addOptions(parser);
    options.stimulus.addOptions(parser);
    return parser;
}

void validate(const Options& options)
{
    if (options.out.empty())
        throw UsageError("option '--out' is required");
    if (options.request.trigger_channel == 2 && options.scope.mode != ScopeMode::Ch1Ch2)
        throw UsageError("triggering on ch2 needs '--mode scope2'");
    if (options.request.pre_s + options.request.post_s <= 0)
        throw UsageError("'--pre' and '--post' cannot both be zero");
    if (options.stimulus.given)
        options.stimulus.validate();
}

std::string describeCaptureError(int rc)
{
    switch (rc)
    {
    case -3:
        return "the trigger channel is not streaming in this scope mode";
    case -4:
        return "invalid window: '--pre' plus '--post' must fit in the 10 s sample "
               "buffer with 0.5 s to spare";
    case -5:
        return "timed out waiting for the trigger";
    case -6:
        return "the sample stream restarted during the capture";
    default:
        return "librador error " + std::to_string(rc);
    }
}

void writeMetadata(const std::string& path, const Options& options, const DeviceSession& session,
                   const SignalOutput& out1, const SignalOutput& out2,
                   const librador_capture_result& result)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    JsonWriter json(file);
    json.beginObject();
    json.field("sample_rate_hz", result.sample_rate_hz);
    json.field("samples", static_cast<uint64_t>(result.ch1.size()));
    json.field("pre_samples", result.pre_samples);
    json.key("trigger").beginObject();
    json.field("channel", options.request.trigger_channel);
    json.field("edge", options.request.rising ? "rising" : "falling");
    json.field("level_v", options.request.level_v);
    json.field("hysteresis_v", options.request.hysteresis_v);
    json.endObject();
    json.field("gain", options.scope.gain);
    json.field("mode", scopeModeName(options.scope.mode));
    json.field("frames_bad_checksum", result.frames_bad_checksum);
    json.field("frames_dropped", result.frames_dropped);
    session.writeJson(json);
    json.key("stimulus");
    if (options.stimulus.given)
    {
        json.beginObject();
        options.stimulus.writeJson(json, out1, out2);
        json.endObject();
    }
    else
        json.null();
    json.endObject();
    json.finish();
    if (!file)
        throw CommandError("writing '" + path + "' failed");
}
} // namespace

std::string CaptureCommand::name() const
{
    return "capture";
}

std::string CaptureCommand::summary() const
{
    return "Save full-rate scope samples around a trigger edge to a CSV file";
}

void CaptureCommand::printHelp(std::ostream& out, const std::string& program) const
{
    Options unused;
    out << program << " capture - " << summary() << "\n\n"
        << "Arms an edge trigger on one scope channel, waits for it to fire, and writes\n"
        << "the samples from --pre seconds before to --post seconds after the trigger at\n"
        << "the board's full rate (375 kS/s per channel, 750 kS/s in scope1-750k).\n"
        << "\"armed: ...\" appears on stderr once the trigger is waiting; press Ctrl-C to\n"
        << "abort. The signal generators and PSU are left as they are unless --sg1, --sg2\n"
        << "or --psu is given; then all three are set first, in the same session, so a\n"
        << "circuit's response to a known stimulus can be captured in one go.\n\n"
        << "Usage:\n"
        << "  " << program << " capture --out FILE [options]\n\n"
        << "Options:\n";
    makeParser(unused).printOptions(out);
    out << "\nOutput:\n"
        << "  FILE is CSV with header \"t,CH1,CH2\" (\"t,CH1\" in the one-channel modes): t\n"
        << "  is in seconds with 0 at the trigger sample, the channels are volts at the\n"
        << "  input pins. FILE.json records the sample rate, trigger, gain, transport,\n"
        << "  calibration, stimulus and any bad or dropped USB frames seen while waiting.\n\n"
        << signalSpecHelp() << '\n'
        << "Notes:\n"
        << "  Full scale is about 1.65 V +/- 23.65 V / gain; larger signals clip, so\n"
        << "  pick the lowest gain that keeps the signal in range.\n"
        << "  The board can only be open in one program: close the Labrador app first.\n\n"
        << "Examples:\n"
        << "  " << program << " capture --gain 4 --trigger ch1:rising:3.0 \\\n"
        << "      --pre 0.2 --post 0.3 --out capture.csv\n"
        << "  " << program << " capture --sg1 square:100:3:0.2 --trigger ch1:rising:1.5 \\\n"
        << "      --pre 0.01 --post 0.02 --out step.csv\n";
}

int CaptureCommand::run(const std::vector<std::string>& args)
{
    Options options;
    makeParser(options).parse(args);
    validate(options);

    // Opened before arming so an unwritable path fails immediately, not after the wait.
    std::ofstream csv(options.out, std::ios::binary | std::ios::trunc);
    if (!csv)
        throw CommandError("cannot open '" + options.out + "' for writing");

    DeviceSession session(options.scope.verbose, options.scope.transport);
    SignalOutput out1, out2;
    if (options.stimulus.given)
        options.stimulus.apply(out1, out2);
    session.configureScope(options.scope.mode, options.scope.gain, options.scope.settle_s);

    std::cerr << "armed: ch" << options.request.trigger_channel << ' '
              << (options.request.rising ? "rising" : "falling") << " through "
              << options.request.level_v << " V, pre " << options.request.pre_s
              << " s, post " << options.request.post_s << " s" << std::endl;

    librador_capture_result result;
    const int rc = librador_capture_around_trigger(&options.request, &result);
    if (rc != 0)
    {
        csv.close();
        std::remove(options.out.c_str());
        throw CommandError("capture failed: " + describeCaptureError(rc));
    }

    writeSamplesCsv(csv, result.ch1, result.ch2, result.sample_rate_hz, result.pre_samples);
    writeMetadata(options.out + ".json", options, session, out1, out2, result);

    std::cout << "wrote " << options.out << ": " << result.ch1.size()
              << " samples at " << result.sample_rate_hz << " Hz ("
              << (result.ch2.empty() ? "CH1" : "CH1+CH2") << "), trigger at t=0 (sample "
              << result.pre_samples << ")" << std::endl;
    if (result.frames_bad_checksum || result.frames_dropped)
        std::cerr << "warning: " << result.frames_bad_checksum
                  << " bad-checksum and " << result.frames_dropped
                  << " dropped USB frames while waiting; the window may contain "
                     "corrupt or repeated samples\n";
    return EXIT_SUCCESS;
}
