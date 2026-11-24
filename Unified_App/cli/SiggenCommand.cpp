#include "SiggenCommand.h"

#include "DeviceSession.h"
#include "JsonWriter.h"
#include "OptionParser.h"
#include "Stimulus.h"

#include <cstdio>
#include <iostream>

namespace
{
struct Options
{
    Stimulus stimulus;
    bool json = false;
    bool verbose = false;
};

OptionParser makeParser(Options& options)
{
    OptionParser parser;
    options.stimulus.addOptions(parser);
    parser.addFlag("--json", "print what the generators were set to as JSON",
                   [&]() { options.json = true; });
    parser.addFlag("--verbose", "also show librador's debug messages on stderr",
                   [&]() { options.verbose = true; });
    return parser;
}

void describe(std::ostream& out, const char* name, const SignalSpec& spec,
              const SignalOutput& actual)
{
    char text[200];
    if (spec.wave == SignalSpec::Wave::Off)
        std::snprintf(text, sizeof text, "%s off (parked at %.3f V)\n", name, actual.v_min);
    else if (spec.wave == SignalSpec::Wave::Dc)
        std::snprintf(text, sizeof text, "%s %s: %.3f V\n", name, spec.text().c_str(),
                      actual.v_min);
    else
        std::snprintf(text, sizeof text, "%s %s: %.6g Hz, %.3f to %.3f V\n", name,
                      spec.text().c_str(), actual.frequency_hz, actual.v_min, actual.v_max);
    out << text;
}
} // namespace

std::string SiggenCommand::name() const
{
    return "siggen";
}

std::string SiggenCommand::summary() const
{
    return "Set the signal generators' waveform, frequency and levels";
}

void SiggenCommand::printHelp(std::ostream& out, const std::string& program) const
{
    Options unused;
    out << program << " siggen - " << summary() << "\n\n"
        << "Sets the power supply, then signal generator 1 and 2, and prints what each\n"
        << "now outputs after the board's rounding. A generator not given is switched off.\n"
        << "The board keeps generating after the command exits, until another command or\n"
        << "the Labrador app changes the generators (the app switches them off when it\n"
        << "connects) or the board loses power.\n\n"
        << "Usage:\n"
        << "  " << program << " siggen [options]\n\n"
        << "Options:\n";
    makeParser(unused).printOptions(out);
    out << '\n'
        << signalSpecHelp() << '\n'
        << "Examples:\n"
        << "  " << program << " siggen --sg1 sine:1000:2:0.5\n"
        << "  " << program << " siggen --sg1 square:50:3 --sg2 dc:1.2\n"
        << "  " << program << " siggen --psu 8 --sg1 triangle:200:6:0.5\n";
}

int SiggenCommand::run(const std::vector<std::string>& args)
{
    Options options;
    makeParser(options).parse(args);
    options.stimulus.validate();

    DeviceSession session(options.verbose, UsbTransport::Auto);
    SignalOutput out1, out2;
    options.stimulus.apply(out1, out2);

    if (options.json)
    {
        JsonWriter json(std::cout);
        json.beginObject();
        json.field("command", "siggen");
        session.writeJson(json);
        options.stimulus.writeJson(json, out1, out2);
        json.endObject();
        json.finish();
    }
    else
    {
        std::cout << "PSU " << options.stimulus.psu_v << " V\n";
        describe(std::cout, "SG1", options.stimulus.sg1, out1);
        describe(std::cout, "SG2", options.stimulus.sg2, out2);
        std::cout.flush();
    }

    return EXIT_SUCCESS;
}
