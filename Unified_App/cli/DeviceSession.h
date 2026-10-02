#pragma once

#include <string>

enum class ScopeMode
{
    Ch1,   // one channel at 375 kS/s
    Ch1Ch2 // both channels at 375 kS/s each
};

// How USB frames reach the host; see librador's LABRADOR_TRANSPORT_* values.
enum class UsbTransport
{
    Auto, // librador's per-platform choice (six isochronous endpoints on Windows/Linux)
    Iso6, // six isochronous endpoints of 125 bytes per frame
    Iso1  // one isochronous endpoint of 750 bytes per frame
};

// RAII connection to the Labrador board: connects on construction, disconnects on
// destruction. Only one process can own the board, so the Labrador app must be closed.
// librador's diagnostic output is routed to stderr (debug messages only if `verbose`)
// so stdout stays for command results.
class DeviceSession
{
  public:
    // Throws CommandError if no board can be opened.
    explicit DeviceSession(bool verbose, UsbTransport transport);
    ~DeviceSession();

    DeviceSession(const DeviceSession&) = delete;
    DeviceSession& operator=(const DeviceSession&) = delete;

    // Selects the scope mode and hardware gain, then waits `settle_s` for the analog
    // front end to settle. Throws CommandError if the board rejects the gain.
    void configureScope(ScopeMode mode, double gain, double settle_s);

    // Gains the hardware supports: 0.5, 1, 2, 4, 8, 16, 32, 64.
    static bool isValidScopeGain(double gain);

  private:
    bool m_verbose; // read by the log sink through librador's userdata pointer
};
