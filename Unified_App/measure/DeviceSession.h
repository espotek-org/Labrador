#pragma once

#include <cstdint>
#include <string>
#include <vector>

class JsonWriter;

enum class ScopeMode
{
    Ch1,    // one channel at 375 kS/s
    Ch1Ch2, // both channels at 375 kS/s each
    Ch1Fast // one channel at 750 kS/s
};

// How USB frames reach the host; see librador's LABRADOR_TRANSPORT_* values.
enum class UsbTransport
{
    Auto, // librador's per-platform choice: iso6 on Windows/Linux, bulk on macOS
    Iso6, // six isochronous endpoints of 125 bytes per frame
    Iso1, // one isochronous endpoint of 750 bytes per frame
    Bulk  // one bulk endpoint; the only transport librador allows on macOS
};

// Name of a LABRADOR_TRANSPORT_* value: "auto", "iso6", "iso1" or "bulk".
std::string transportName(int librador_transport);
// The --mode spelling: "scope1", "scope2" or "scope1-750k".
std::string scopeModeName(ScopeMode mode);

// Scope and PSU calibration as stored on the board's EEPROM.
struct BoardCalibration
{
    bool stored = false; // false: the board holds none, so the neutral values apply
    double vref_ch1 = 1.65;
    double gain_scale_ch1 = 1.0;
    double vref_ch2 = 1.65;
    double gain_scale_ch2 = 1.0;
    double psu_offset_v = 0.0;

    // Writes {"stored": ..., "vref_ch1": ..., ...} as the current value.
    void writeJson(JsonWriter& json) const;
};

// USB frame counters (librador_get_frame_stats) over some stretch of streaming.
struct FrameStats
{
    uint64_t ok = 0;
    uint64_t bad_checksum = 0; // frames corrupted on the board (see librador.h)
    uint64_t dropped = 0;      // frames lost in transit, filled with a repeat
    uint64_t unvalidated = 0;  // frames whose checksum record never arrived (iso only)

    static FrameStats now();   // the counters since connecting
    FrameStats since(const FrameStats& earlier) const;
    // {"ok": ..., "bad_checksum": ..., "dropped": ..., "unvalidated": ...}
    void writeJson(JsonWriter& json) const;
};

// A contiguous run of full-rate samples from the scope.
struct SampleWindow
{
    double sample_rate_hz = 0;
    std::vector<double> ch1;
    std::vector<double> ch2; // empty unless CH2 streams in the current mode
    FrameStats frames;       // counted while the window arrived
};

// librador_init/librador_exit for commands that manage the connection themselves
// (update-firmware, info), with librador's log routed to stderr like DeviceSession.
class LibradorScope
{
  public:
    // Throws std::runtime_error if librador cannot start. `verbose` must outlive this.
    LibradorScope(const bool* verbose, UsbTransport transport);
    ~LibradorScope();
    LibradorScope(const LibradorScope&) = delete;
    LibradorScope& operator=(const LibradorScope&) = delete;
};

// RAII connection to the Labrador board: connects on construction, disconnects on
// destruction. Only one process can own the board, so the Labrador app must be closed.
// librador's diagnostic output is routed to stderr (debug messages only if `verbose`)
// so stdout stays for command results. Like the app, it applies the calibration
// stored on the board, so voltages read the same as in the app.
class DeviceSession
{
  public:
    // Throws std::runtime_error if no board can be opened, it runs other firmware, or its
    // stored calibration cannot be read.
    explicit DeviceSession(bool verbose, UsbTransport transport);
    ~DeviceSession();

    DeviceSession(const DeviceSession&) = delete;
    DeviceSession& operator=(const DeviceSession&) = delete;

    // Selects the scope mode and hardware gain, then waits `settle_s` for the analog
    // front end to settle. Throws std::runtime_error if the board rejects either.
    void configureScope(ScopeMode mode, double gain, double settle_s);

    // Waits for the next `seconds` of samples (at most 9.5 s, what the board's
    // 10 s buffer holds with slack) and returns them. Throws std::runtime_error if the
    // stream stalls or restarts meanwhile.
    SampleWindow acquire(double seconds) const;

    // The LABRADOR_TRANSPORT_* value librador resolved the request to.
    int activeTransport() const;
    const BoardCalibration& calibration() const { return m_calibration; }

    // "transport", "firmware" and "calibration" members of the current JSON object.
    void writeJson(JsonWriter& json) const;

    // Gains the hardware supports: 0.5, 1, 2, 4, 8, 16, 32, 64.
    static bool isValidScopeGain(double gain);

    // Reads the calibration stored on the connected board (read only). Throws
    // std::runtime_error on a USB error.
    static BoardCalibration readCalibration();

    // Route librador's log to stderr (debug lines only while *verbose is true);
    // resetLogs() detaches it. `verbose` must outlive the routing.
    static void routeLogsToStderr(const bool* verbose);
    static void resetLogs();

  private:
    bool m_verbose; // read by the log sink through librador's userdata pointer
    LibradorScope m_librador;
    BoardCalibration m_calibration;
};
