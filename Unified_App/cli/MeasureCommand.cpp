#include "MeasureCommand.h"

#include "DeviceSession.h"
#include "JsonWriter.h"
#include "OptionParser.h"
#include "SampleCsv.h"
#include "ScopeOptions.h"
#include "Stimulus.h"
#include "SignalStats.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>

namespace
{
struct Options
{
    ScopeOptions scope;
    Stimulus stimulus;
    double seconds = 0.1;
    std::string csv; // empty: no CSV
    bool json = false;
};

OptionParser makeParser(Options& options)
{
    const Options defaults;
    OptionParser parser;
    parser.addOption("--seconds", "S",
                     "length of the window to measure, up to 9.5 (4.5 in scope1-750k); "
                     "default " + OptionParser::formatNumber(defaults.seconds),
                     [&](const std::string& v)
                     { options.seconds = OptionParser::toNonNegative("--seconds", v); });
    options.stimulus.addOptions(parser);
    options.scope.addOptions(parser);
    parser.addOption("--csv", "FILE", "also save the window's samples to FILE as CSV",
                     [&](const std::string& v) { options.csv = v; });
    parser.addFlag("--json", "print the results as JSON instead of text",
                   [&]() { options.json = true; });
    return parser;
}

std::string describeOutput(const SignalSpec& spec, const SignalOutput& out)
{
    char text[160];
    if (spec.wave == SignalSpec::Wave::Off)
        std::snprintf(text, sizeof text, "off (%.3f V)", out.v_min);
    else if (spec.wave == SignalSpec::Wave::Dc)
        std::snprintf(text, sizeof text, "%s (actual %.3f V)", spec.text().c_str(), out.v_min);
    else
        std::snprintf(text, sizeof text, "%s (actual %.6g Hz, %.3f to %.3f V)",
                      spec.text().c_str(), out.frequency_hz, out.v_min, out.v_max);
    return text;
}

void printStats(std::ostream& out, const char* channel, const ChannelStats& s)
{
    char text[256];
    std::snprintf(text, sizeof text,
                  "%s: %llu samples, min %.4f V, max %.4f V, mean %.4f V, Vpp %.4f V, RMS(ac) "
                  "%.4f V",
                  channel, static_cast<unsigned long long>(s.samples), s.min_v, s.max_v, s.mean_v, s.vpp, s.rms_ac_v);
    out << text;
    if (s.periodic)
    {
        std::snprintf(text, sizeof text, ", %.6g Hz over %d cycles, duty %.1f%%",
                      s.frequency_hz, s.cycles, s.duty * 100);
        out << text;
    }
    else
        out << ", not periodic";
    out << '\n';
}
} // namespace

std::string MeasureCommand::name() const
{
    return "measure";
}

std::string MeasureCommand::summary() const
{
    return "Drive the signal generators and measure what the scope sees";
}

void MeasureCommand::printHelp(std::ostream& out, const std::string& program) const
{
    Options unused;
    out << program << " measure - " << summary() << "\n\n"
        << "Sets the power supply and both signal generators (if asked), configures the\n"
        << "scope, waits --settle seconds, then records the next --seconds of samples and\n"
        << "reports, per channel, the minimum, maximum, mean, peak-to-peak and AC RMS\n"
        << "voltage and, for a periodic signal, the frequency (from rising crossings of\n"
        << "the mid level) and duty cycle. Everything happens in one session, so nothing\n"
        << "depends on what the board was left doing before.\n\n"
        << "Usage:\n"
        << "  " << program << " measure [options]\n\n"
        << "Options:\n";
    makeParser(unused).printOptions(out);
    out << '\n'
        << signalSpecHelp() << '\n'
        << "Examples:\n"
        << "  " << program << " measure --sg1 sine:1000:2:0.5      # with SG1 wired to CH1\n"
        << "  " << program << " measure --mode scope1 --gain 16 --seconds 1 --json\n";
}

int MeasureCommand::run(const std::vector<std::string>& args)
{
    Options options;
    makeParser(options).parse(args);
    options.stimulus.validate();

    // Opened before connecting so an unwritable path fails before any USB work.
    std::unique_ptr<std::ofstream> csv;
    if (!options.csv.empty())
    {
        csv = std::make_unique<std::ofstream>(options.csv, std::ios::binary | std::ios::trunc);
        if (!*csv)
            throw CommandError("cannot open '" + options.csv + "' for writing");
    }

    DeviceSession session(options.scope.verbose, options.scope.transport);
    SignalOutput out1, out2;
    options.stimulus.apply(out1, out2);
    session.configureScope(options.scope.mode, options.scope.gain, options.scope.settle_s);
    const SampleWindow window = session.acquire(options.seconds);

    const double lsb = scopeLsbVolts(options.scope.gain);
    const ChannelStats ch1 = analyseChannel(window.ch1, window.sample_rate_hz, lsb);
    const bool have_ch2 = !window.ch2.empty();
    const ChannelStats ch2 =
        have_ch2 ? analyseChannel(window.ch2, window.sample_rate_hz, lsb) : ChannelStats();

    if (csv)
        writeSamplesCsv(*csv, window.ch1, window.ch2, window.sample_rate_hz, 0);

    if (options.json)
    {
        JsonWriter json(std::cout);
        json.beginObject();
        json.field("command", "measure");
        session.writeJson(json);
        json.field("mode", scopeModeName(options.scope.mode));
        json.field("gain", options.scope.gain);
        json.field("lsb_v", lsb);
        json.field("settle_s", options.scope.settle_s);
        json.field("seconds", options.seconds);
        json.field("sample_rate_hz", window.sample_rate_hz);
        json.key("stimulus").beginObject();
        options.stimulus.writeJson(json, out1, out2);
        json.endObject();
        json.key("ch1");
        ch1.writeJson(json);
        json.key("ch2");
        if (have_ch2)
            ch2.writeJson(json);
        else
            json.null();
        json.key("frames");
        window.frames.writeJson(json);
        if (!options.csv.empty())
            json.field("csv", options.csv);
        json.endObject();
        json.finish();
    }
    else
    {
        std::cout << "SG1 " << describeOutput(options.stimulus.sg1, out1) << ", SG2 "
                  << describeOutput(options.stimulus.sg2, out2) << ", PSU "
                  << options.stimulus.psu_v << " V\n";
        std::cout << "scope " << scopeModeName(options.scope.mode) << " at gain "
                  << options.scope.gain << ", " << window.sample_rate_hz << " S/s over "
                  << transportName(session.activeTransport())
                  << (session.calibration().stored ? ", board calibration applied"
                                                   : ", board holds no calibration")
                  << '\n';
        printStats(std::cout, "CH1", ch1);
        if (have_ch2)
            printStats(std::cout, "CH2", ch2);
        std::cout << "USB frames: " << window.frames.ok << " ok, " << window.frames.bad_checksum
                  << " bad checksum, " << window.frames.dropped << " dropped, "
                  << window.frames.unvalidated << " unvalidated" << std::endl;
    }
    if (window.frames.bad_checksum || window.frames.dropped)
        std::cerr << "warning: " << window.frames.bad_checksum << " bad-checksum and "
                  << window.frames.dropped
                  << " dropped USB frames during the window; the samples may contain "
                     "corrupt or repeated stretches\n";
    return EXIT_SUCCESS;
}
