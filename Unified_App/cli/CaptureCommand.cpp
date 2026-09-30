#include "CaptureCommand.h"

#include "DeviceSession.h"
#include "OptionParser.h"

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
    ScopeMode mode = ScopeMode::Ch1Ch2;
    double gain = 4;
    double settle_s = 0.5;
    bool verbose = false;
    librador_capture_request request; // trigger, hysteresis, pre/post, timeout
};

std::string formatNumber(double value)
{
    std::ostringstream text;
    text << value;
    return text.str();
}

double parseSeconds(const std::string& option, const std::string& text)
{
    const double value = OptionParser::toDouble(option, text);
    if (value < 0)
        throw UsageError("option '" + option + "' must not be negative");
    return value;
}

ScopeMode parseMode(const std::string& text)
{
    if (text == "scope1")
        return ScopeMode::Ch1;
    if (text == "scope2")
        return ScopeMode::Ch1Ch2;
    throw UsageError("option '--mode' must be scope1 or scope2, got '" + text + "'");
}

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
    parser.addOption("--mode", "MODE",
                     "scope1 (CH1 only) or scope2 (CH1 and CH2); default scope2",
                     [&](const std::string& v) { options.mode = parseMode(v); });
    parser.addOption("--gain", "G",
                     "hardware gain 0.5, 1, 2, 4, 8, 16, 32 or 64; default " +
                         formatNumber(defaults.gain),
                     [&](const std::string& v)
                     {
                         const double gain = OptionParser::toDouble("--gain", v);
                         if (!DeviceSession::isValidScopeGain(gain))
                             throw UsageError("option '--gain' must be one of 0.5, 1, "
                                              "2, 4, 8, 16, 32, 64, got '" +
                                              v + "'");
                         options.gain = gain;
                     });
    parser.addOption("--trigger", "CH:EDGE:VOLTS",
                     "channel (ch1|ch2), edge (rising|falling), level; default "
                     "ch1:rising:" + formatNumber(d.level_v),
                     [&](const std::string& v) { parseTrigger(v, options.request); });
    parser.addOption("--hysteresis", "VOLTS",
                     "signal must leave the level by this much to re-arm; default " +
                         formatNumber(d.hysteresis_v),
                     [&](const std::string& v)
                     { options.request.hysteresis_v = parseSeconds("--hysteresis", v); });
    parser.addOption("--pre", "SECONDS",
                     "keep this long before the trigger; default " +
                         formatNumber(d.pre_s),
                     [&](const std::string& v)
                     { options.request.pre_s = parseSeconds("--pre", v); });
    parser.addOption("--post", "SECONDS",
                     "keep this long after the trigger; default " +
                         formatNumber(d.post_s),
                     [&](const std::string& v)
                     { options.request.post_s = parseSeconds("--post", v); });
    parser.addOption("--timeout", "SECONDS",
                     "give up if no trigger; 0 waits forever (default)",
                     [&](const std::string& v)
                     { options.request.timeout_s = parseSeconds("--timeout", v); });
    parser.addOption("--settle", "SECONDS",
                     "pause after configuring the scope; default " +
                         formatNumber(defaults.settle_s),
                     [&](const std::string& v)
                     { options.settle_s = parseSeconds("--settle", v); });
    parser.addFlag("--verbose", "also show librador's debug messages on stderr",
                   [&]() { options.verbose = true; });
    return parser;
}

void validate(const Options& options)
{
    if (options.out.empty())
        throw UsageError("option '--out' is required");
    if (options.request.trigger_channel == 2 && options.mode == ScopeMode::Ch1)
        throw UsageError("triggering on ch2 needs '--mode scope2'");
    if (options.request.pre_s + options.request.post_s <= 0)
        throw UsageError("'--pre' and '--post' cannot both be zero");
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

void writeCsv(std::ostream& out, const librador_capture_result& result)
{
    const bool two_channels = !result.ch2.empty();
    out << (two_channels ? "t,CH1,CH2\n" : "t,CH1\n");

    char line[96];
    for (size_t i = 0; i < result.ch1.size(); i++)
    {
        const double t = (static_cast<double>(i) - result.pre_samples) /
                         result.sample_rate_hz;
        const int length =
            two_channels
                ? std::snprintf(line, sizeof line, "%.7f,%.4f,%.4f\n", t,
                                result.ch1[i], result.ch2[i])
                : std::snprintf(line, sizeof line, "%.7f,%.4f\n", t, result.ch1[i]);
        out.write(line, length);
    }
    out.flush();
    if (!out)
        throw CommandError("writing the CSV failed");
}

void writeMetadata(const std::string& path, const Options& options,
                   const librador_capture_result& result)
{
    std::ofstream json(path, std::ios::binary | std::ios::trunc);
    json << "{\n"
         << "  \"sample_rate_hz\": " << result.sample_rate_hz << ",\n"
         << "  \"samples\": " << result.ch1.size() << ",\n"
         << "  \"pre_samples\": " << result.pre_samples << ",\n"
         << "  \"trigger\": {\"channel\": " << options.request.trigger_channel
         << ", \"edge\": \"" << (options.request.rising ? "rising" : "falling")
         << "\", \"level_v\": " << options.request.level_v
         << ", \"hysteresis_v\": " << options.request.hysteresis_v << "},\n"
         << "  \"gain\": " << options.gain << ",\n"
         << "  \"mode\": \""
         << (options.mode == ScopeMode::Ch1 ? "scope1" : "scope2") << "\",\n"
         << "  \"frames_bad_checksum\": " << result.frames_bad_checksum << ",\n"
         << "  \"frames_dropped\": " << result.frames_dropped << "\n"
         << "}\n";
    json.flush();
    if (!json)
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
        << "the board's full rate (375 kS/s per channel). \"armed: ...\" appears on\n"
        << "stderr once the trigger is waiting; press Ctrl-C to abort.\n\n"
        << "Usage:\n"
        << "  " << program << " capture --out FILE [options]\n\n"
        << "Options:\n";
    makeParser(unused).printOptions(out);
    out << "\nOutput:\n"
        << "  FILE is CSV with header \"t,CH1,CH2\" (\"t,CH1\" in scope1 mode): t is in\n"
        << "  seconds with 0 at the trigger sample, the channels are volts at the input\n"
        << "  pins. FILE.json records the sample rate, trigger, gain and any bad or\n"
        << "  dropped USB frames seen while waiting.\n\n"
        << "Notes:\n"
        << "  Full scale is about 1.65 V +/- 23.65 V / gain; larger signals clip, so\n"
        << "  pick the lowest gain that keeps the signal in range.\n"
        << "  The board can only be open in one program: close the Labrador app first.\n\n"
        << "Example:\n"
        << "  " << program << " capture --gain 4 --trigger ch1:rising:3.0 \\\n"
        << "      --pre 0.2 --post 0.3 --out capture.csv\n";
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

    DeviceSession session(options.verbose);
    session.configureScope(options.mode, options.gain, options.settle_s);

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

    writeCsv(csv, result);
    writeMetadata(options.out + ".json", options, result);

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
