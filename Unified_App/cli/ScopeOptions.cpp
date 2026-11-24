#include "ScopeOptions.h"

#include "Command.h"
#include "OptionParser.h"

namespace
{
ScopeMode parseMode(const std::string& text)
{
    for (const ScopeMode mode : {ScopeMode::Ch1, ScopeMode::Ch1Ch2, ScopeMode::Ch1Fast})
        if (text == scopeModeName(mode))
            return mode;
    throw UsageError("option '--mode' must be scope1, scope2 or scope1-750k, got '" + text +
                     "'");
}

UsbTransport parseTransport(const std::string& text)
{
    if (text == "auto")
        return UsbTransport::Auto;
    if (text == "iso6")
        return UsbTransport::Iso6;
    if (text == "iso1")
        return UsbTransport::Iso1;
    if (text == "bulk")
        return UsbTransport::Bulk;
    throw UsageError("option '--transport' must be auto, iso6, iso1 or bulk, got '" + text +
                     "'");
}
} // namespace

void ScopeOptions::addOptions(OptionParser& parser)
{
    const ScopeOptions defaults;
    parser.addOption("--mode", "MODE",
                     "scope1 (CH1 only), scope2 (CH1 and CH2, 375 kS/s each) or "
                     "scope1-750k (CH1 only at 750 kS/s); default " +
                         scopeModeName(defaults.mode),
                     [this](const std::string& v) { mode = parseMode(v); });
    parser.addOption("--gain", "G",
                     "hardware gain 0.5, 1, 2, 4, 8, 16, 32 or 64; default " +
                         OptionParser::formatNumber(defaults.gain),
                     [this](const std::string& v)
                     {
                         const double value = OptionParser::toDouble("--gain", v);
                         if (!DeviceSession::isValidScopeGain(value))
                             throw UsageError("option '--gain' must be one of 0.5, 1, "
                                              "2, 4, 8, 16, 32, 64, got '" +
                                              v + "'");
                         gain = value;
                     });
    parser.addOption("--settle", "SECONDS",
                     "pause after configuring the board; default " +
                         OptionParser::formatNumber(defaults.settle_s),
                     [this](const std::string& v) { settle_s = OptionParser::toNonNegative("--settle", v); });
    parser.addOption("--transport", "T",
                     "USB transport: auto (the platform default: iso6 on Windows and "
                     "Linux, bulk on macOS), iso6, iso1 or bulk; default auto",
                     [this](const std::string& v) { transport = parseTransport(v); });
    parser.addFlag("--verbose", "also show librador's debug messages on stderr",
                   [this]() { verbose = true; });
}
