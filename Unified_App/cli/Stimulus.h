#pragma once

#include "Signal.h"

#include <string>

class JsonWriter;
class OptionParser;

// Parses a --sg1/--sg2 value: "off", "dc:VOLTS" or "WAVE:FREQ:VPP[:BASE]" with
// WAVE sine, square, triangle or sawtooth. Throws UsageError naming `option` for
// anything malformed or outside the generators' range.
SignalSpec parseSignalSpec(const std::string& option, const std::string& text);

// The stimulus options shared by every command that drives the generators:
// --sg1 SPEC, --sg2 SPEC and --psu VOLTS.
struct Stimulus
{
    SignalSpec sg1;
    SignalSpec sg2;
    double psu_v = PSU_MIN_V;
    bool given = false; // any of the three options appeared on the command line

    // Registers --sg1, --sg2 and --psu on `parser`.
    void addOptions(OptionParser& parser);
    // UsageError if a signal's peak needs more PSU voltage than --psu gives it.
    void validate() const;

    // Over an open DeviceSession: sets the PSU, then both generators, and
    // returns what each generator outputs. Throws if the board refuses a setting.
    void apply(SignalOutput& out1, SignalOutput& out2) const;

    // {"psu_v": ..., "sg1": {...}, "sg2": {...}} members of the current object.
    void writeJson(JsonWriter& json, const SignalOutput& out1, const SignalOutput& out2) const;
};

// Help text describing the SPEC syntax, for the commands' --help.
std::string signalSpecHelp();
