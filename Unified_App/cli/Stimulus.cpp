#include "Stimulus.h"

#include "Command.h"
#include "JsonWriter.h"
#include "OptionParser.h"

#include <sstream>
#include <vector>

namespace
{
void requireRange(const std::string& option, const std::string& spec, const char* what,
                  double value, double lo, double hi, const char* unit)
{
    if (value < lo || value > hi)
        throw UsageError("option '" + option + "': " + what + " in '" + spec +
                         "' must be " + OptionParser::formatNumber(lo) + " to " +
                         OptionParser::formatNumber(hi) + " " + unit);
}
} // namespace

SignalSpec parseSignalSpec(const std::string& option, const std::string& text)
{
    std::vector<std::string> parts;
    std::istringstream in(text);
    for (std::string part; std::getline(in, part, ':');)
        parts.push_back(part);
    if (!text.empty() && text.back() == ':')
        parts.push_back("");

    SignalSpec spec;
    if (parts.size() == 1 && parts[0] == "off")
        return spec;
    if (parts.size() == 2 && parts[0] == "dc")
    {
        spec.wave = SignalSpec::Wave::Dc;
        spec.base_v = OptionParser::toDouble(option, parts[1]);
        requireRange(option, text, "the level", spec.base_v, 0, SIGNAL_MAX_PEAK_V, "V");
        return spec;
    }
    if (parts.size() == 3 || parts.size() == 4)
    {
        if (parts[0] == "sine")
            spec.wave = SignalSpec::Wave::Sine;
        else if (parts[0] == "square")
            spec.wave = SignalSpec::Wave::Square;
        else if (parts[0] == "triangle")
            spec.wave = SignalSpec::Wave::Triangle;
        else if (parts[0] == "sawtooth")
            spec.wave = SignalSpec::Wave::Sawtooth;
    }
    if (spec.wave == SignalSpec::Wave::Off)
        throw UsageError("option '" + option + "' expects off, dc:VOLTS or "
                         "WAVE:FREQ:VPP[:BASE] (WAVE sine, square, triangle or "
                         "sawtooth), got '" + text + "'");

    spec.frequency_hz = OptionParser::toDouble(option, parts[1]);
    spec.vpp = OptionParser::toDouble(option, parts[2]);
    spec.base_v = parts.size() == 4 ? OptionParser::toDouble(option, parts[3]) : 0.0;
    requireRange(option, text, "the frequency", spec.frequency_hz, SIGNAL_MIN_FREQUENCY_HZ,
                 SIGNAL_MAX_FREQUENCY_HZ, "Hz");
    requireRange(option, text, "the peak-to-peak voltage", spec.vpp, SIGNAL_MIN_VPP, SIGNAL_MAX_VPP, "V");
    requireRange(option, text, "the base voltage", spec.base_v, 0, SIGNAL_MAX_BASE_V, "V");
    if (spec.peakV() > SIGNAL_MAX_PEAK_V)
        throw UsageError("option '" + option + "': '" + text + "' peaks at " +
                         OptionParser::formatNumber(spec.peakV()) + " V; the generators reach " +
                         OptionParser::formatNumber(SIGNAL_MAX_PEAK_V) + " V at most");
    return spec;
}

void Stimulus::addOptions(OptionParser& parser)
{
    parser.addOption("--sg1", "SPEC", "drive signal generator 1 (see SPEC below); default off",
                     [this](const std::string& v)
                     {
                         sg1 = parseSignalSpec("--sg1", v);
                         given = true;
                     });
    parser.addOption("--sg2", "SPEC", "drive signal generator 2; default off",
                     [this](const std::string& v)
                     {
                         sg2 = parseSignalSpec("--sg2", v);
                         given = true;
                     });
    parser.addOption("--psu", "VOLTS",
                     "power supply voltage, " + OptionParser::formatNumber(PSU_MIN_V) +
                         " to " + OptionParser::formatNumber(PSU_MAX_V) + "; default " +
                         OptionParser::formatNumber(PSU_MIN_V) +
                         " (the generators' peak must stay " +
                         OptionParser::formatNumber(SIGGEN_PSU_HEADROOM_V) + " V below it)",
                     [this](const std::string& v)
                     {
                         psu_v = OptionParser::toDouble("--psu", v);
                         if (psu_v < PSU_MIN_V || psu_v > PSU_MAX_V)
                             throw UsageError("option '--psu' must be " +
                                              OptionParser::formatNumber(PSU_MIN_V) + " to " +
                                              OptionParser::formatNumber(PSU_MAX_V) +
                                              " V, got '" + v + "'");
                         given = true;
                     });
}

void Stimulus::validate() const
{
    const double limit = psu_v - SIGGEN_PSU_HEADROOM_V;
    const SignalSpec* specs[2] = {&sg1, &sg2};
    for (int i = 0; i < 2; i++)
        if (specs[i]->peakV() > limit + 1e-9)
            throw UsageError("SG" + std::to_string(i + 1) + " '" + specs[i]->text() +
                             "' peaks at " + OptionParser::formatNumber(specs[i]->peakV()) +
                             " V, but with the PSU at " + OptionParser::formatNumber(psu_v) +
                             " V the generators reach " + OptionParser::formatNumber(limit) +
                             " V; raise '--psu' to at least " +
                             OptionParser::formatNumber(specs[i]->peakV() + SIGGEN_PSU_HEADROOM_V));
}

void Stimulus::apply(SignalOutput& out1, SignalOutput& out2) const
{
    applyPsu(psu_v);
    out1 = applySignal(1, sg1);
    out2 = applySignal(2, sg2);
}

void Stimulus::writeJson(JsonWriter& json, const SignalOutput& out1,
                         const SignalOutput& out2) const
{
    json.field("psu_v", psu_v);
    json.key("sg1");
    writeSignalJson(json, sg1, out1);
    json.key("sg2");
    writeSignalJson(json, sg2, out2);
}

std::string signalSpecHelp()
{
    return "SPEC (for --sg1 and --sg2):\n"
           "  off                    output parked at the DAC floor (about 0.06 V), as the\n"
           "                         app's generator off switch does\n"
           "  dc:VOLTS               a constant level, 0 to 9.6 V\n"
           "  WAVE:FREQ:VPP[:BASE]   WAVE is sine, square, triangle or sawtooth; FREQ 1 Hz\n"
           "                         to 100 kHz; VPP the peak-to-peak swing, 0.15 to 9 V;\n"
           "                         BASE the lowest voltage, 0 to 9 V (default 0), the\n"
           "                         app's Vbase. The peak, BASE + VPP, must stay 1.18 V\n"
           "                         below the --psu voltage and at most 9.6 V.\n"
           "  The output cannot go below about 0.06 V, so a BASE under that is raised\n"
           "  and the swing shrinks to match; the frequency is rounded to what the\n"
           "  generator's timer can make. Both appear as \"actual\" in the output.\n";
}
