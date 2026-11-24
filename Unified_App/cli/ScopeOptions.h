#pragma once

#include "DeviceSession.h"

class OptionParser;

// Scope options shared by capture, measure and stream-test, parsed and described
// the same way in each: --mode, --gain, --settle, --transport and --verbose.
struct ScopeOptions
{
    ScopeMode mode = ScopeMode::Ch1Ch2;
    double gain = 4;
    double settle_s = 0.5;
    UsbTransport transport = UsbTransport::Auto; // librador's per-platform choice
    bool verbose = false;

    void addOptions(OptionParser& parser);
};
