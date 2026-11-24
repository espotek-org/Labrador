#include "DeviceSession.h"

#include <stdexcept>
#include "JsonWriter.h"

#include "librador.h"

#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <stdexcept>
#include <thread>

namespace
{
constexpr std::array<double, 8> SCOPE_GAINS = {0.5, 1, 2, 4, 8, 16, 32, 64};

// librador device modes: 0 = CH1 scope, 2 = CH1 + CH2 scope, 6 = CH1 scope at 750 kS/s.
constexpr int DEVICE_MODE_CH1 = 0;
constexpr int DEVICE_MODE_CH1_CH2 = 2;
constexpr int DEVICE_MODE_CH1_FAST = 6;

void logToStderr(void* userdata, const int level, const char* format, va_list args)
{
    const bool verbose = *static_cast<const bool*>(userdata);
    if (level == LOG_DEBUG && !verbose)
        return;
    std::vfprintf(stderr, format, args);
}

int toLibradorTransport(UsbTransport transport)
{
    switch (transport)
    {
    case UsbTransport::Iso6: return LABRADOR_TRANSPORT_ISO6;
    case UsbTransport::Iso1: return LABRADOR_TRANSPORT_ISO1;
    case UsbTransport::Bulk: return LABRADOR_TRANSPORT_BULK;
    case UsbTransport::Auto: return LABRADOR_TRANSPORT_AUTO;
    }
    throw std::logic_error("unknown transport");
}
} // namespace

std::string transportName(int librador_transport)
{
    switch (librador_transport)
    {
    case LABRADOR_TRANSPORT_AUTO: return "auto";
    case LABRADOR_TRANSPORT_ISO6: return "iso6";
    case LABRADOR_TRANSPORT_ISO1: return "iso1";
    case LABRADOR_TRANSPORT_BULK: return "bulk";
    }
    throw std::runtime_error("librador reports unknown transport " +
                       std::to_string(librador_transport));
}

std::string scopeModeName(ScopeMode mode)
{
    switch (mode)
    {
    case ScopeMode::Ch1: return "scope1";
    case ScopeMode::Ch1Ch2: return "scope2";
    case ScopeMode::Ch1Fast: return "scope1-750k";
    }
    throw std::logic_error("unknown scope mode");
}

void BoardCalibration::writeJson(JsonWriter& json) const
{
    json.beginObject();
    json.field("stored", stored);
    json.field("vref_ch1", vref_ch1);
    json.field("gain_scale_ch1", gain_scale_ch1);
    json.field("vref_ch2", vref_ch2);
    json.field("gain_scale_ch2", gain_scale_ch2);
    json.field("psu_offset_v", psu_offset_v);
    json.endObject();
}

LibradorScope::LibradorScope(const bool* verbose, UsbTransport transport)
{
    DeviceSession::routeLogsToStderr(verbose);
    if (librador_init(toLibradorTransport(transport)) < 0)
    {
        DeviceSession::resetLogs();
        throw std::runtime_error("could not initialise librador");
    }
}

LibradorScope::~LibradorScope()
{
    librador_exit();
    DeviceSession::resetLogs();
}

DeviceSession::DeviceSession(bool verbose, UsbTransport transport)
    : m_verbose(verbose), m_librador(&m_verbose, transport)
{
    const int rc = librador_connect();
    if (rc < 0 || !librador_is_connected())
        throw std::runtime_error("could not connect to a Labrador board (code " +
                           std::to_string(rc) +
                           "); is it plugged in and not open in another program?");
    if (rc > 0)
    {
        // The board is open but on a firmware librador will not stream from. The
        // GUI handles this through its flash hook; the CLI has none, so it must stop
        // here or wait forever on an empty sample buffer.
        char message[160];
        std::snprintf(message, sizeof message,
                      "the board is running firmware 0x%04x variant %u but this build "
                      "needs 0x%04x variant %u; run 'labrador-cli update-firmware' first",
                      librador_get_device_firmware_version(),
                      librador_get_device_firmware_variant(), EXPECTED_FIRMWARE_VERSION,
                      DEFINED_EXPECTED_VARIANT);
        throw std::runtime_error(message);
    }

    // The app applies the board's stored calibration on every connect
    // (CalibrationControl::loadFromDevice); do the same so readings agree.
    m_calibration = readCalibration();
    if (m_calibration.stored)
    {
        librador_set_channel_calibration(1, m_calibration.vref_ch1, m_calibration.gain_scale_ch1);
        librador_set_channel_calibration(2, m_calibration.vref_ch2, m_calibration.gain_scale_ch2);
        librador_set_psu_calibration_offset(m_calibration.psu_offset_v);
    }
}

DeviceSession::~DeviceSession() = default;

BoardCalibration DeviceSession::readCalibration()
{
    // Right after connecting, the calibration request occasionally stalls
    // (LIBUSB_ERROR_PIPE; seen on 2 of ~60 connects while the host was heavily
    // loaded) and succeeds when repeated, so it gets three tries.
    constexpr int ATTEMPTS = 3;
    BoardCalibration cal;
    int rc = 0;
    for (int attempt = 1; attempt <= ATTEMPTS; attempt++)
    {
        rc = librador_load_calibration_from_device(&cal.vref_ch1, &cal.gain_scale_ch1,
                                                   &cal.vref_ch2, &cal.gain_scale_ch2,
                                                   &cal.psu_offset_v);
        if (rc >= 0)
            break;
        if (attempt < ATTEMPTS)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (rc < 0)
        throw std::runtime_error("reading the board's stored calibration failed " +
                           std::to_string(ATTEMPTS) + " times (code " + std::to_string(rc) +
                           ")");
    cal.stored = rc == 0; // 1: nothing valid stored, the neutral defaults stand
    return cal;
}

void DeviceSession::configureScope(ScopeMode mode, double gain, double settle_s)
{
    if (librador_set_oscilloscope_gain(gain) < 0)
        throw std::runtime_error("the board rejected gain " + std::to_string(gain));
    const int device_mode = mode == ScopeMode::Ch1      ? DEVICE_MODE_CH1
                            : mode == ScopeMode::Ch1Fast ? DEVICE_MODE_CH1_FAST
                                                         : DEVICE_MODE_CH1_CH2;
    const int rc = librador_set_device_mode(device_mode);
    if (rc != 0)
        throw std::runtime_error("the board rejected scope mode " + scopeModeName(mode) +
                           " (code " + std::to_string(rc) + ")");
    std::this_thread::sleep_for(std::chrono::duration<double>(settle_s));
}

FrameStats FrameStats::now()
{
    FrameStats stats;
    if (librador_get_frame_stats(&stats.ok, &stats.bad_checksum, &stats.dropped,
                                 &stats.unvalidated) < 0)
        throw std::runtime_error("cannot read the USB frame counters");
    return stats;
}

FrameStats FrameStats::since(const FrameStats& earlier) const
{
    FrameStats delta;
    delta.ok = ok - earlier.ok;
    delta.bad_checksum = bad_checksum - earlier.bad_checksum;
    delta.dropped = dropped - earlier.dropped;
    delta.unvalidated = unvalidated - earlier.unvalidated;
    return delta;
}

void FrameStats::writeJson(JsonWriter& json) const
{
    json.beginObject();
    json.field("ok", ok);
    json.field("bad_checksum", bad_checksum);
    json.field("dropped", dropped);
    json.field("unvalidated", unvalidated);
    json.endObject();
}

SampleWindow DeviceSession::acquire(double seconds) const
{
    SampleWindow window;
    window.sample_rate_hz = librador_get_samples_per_second();
    if (window.sample_rate_hz <= 0)
        throw std::runtime_error("the board is not streaming");
    const int64_t n = std::llround(seconds * window.sample_rate_hz);
    // The buffers hold 10 s at 375 kS/s; keep half a second spare so the start of
    // the window is not overwritten before the copy.
    const int64_t capacity = 375000LL * 10 - std::llround(window.sample_rate_hz / 2);
    if (n <= 0 || n > capacity)
        throw std::runtime_error("a window of " + std::to_string(seconds) +
                           " s does not fit in the board's sample buffer");

    uint64_t start;
    if (librador_get_analog_sample_count(1, &start) != 0)
        throw std::runtime_error("CH1 is not streaming");
    const FrameStats before = FrameStats::now();

    // Wait for the samples, giving up if the stream stops moving for 2 s.
    using clock = std::chrono::steady_clock;
    uint64_t count = start;
    uint64_t last = start;
    auto last_progress = clock::now();
    while (count < start + static_cast<uint64_t>(n))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        if (librador_get_analog_sample_count(1, &count) != 0 || count < last)
            throw std::runtime_error("the sample stream restarted while waiting for the window");
        if (count != last)
        {
            last = count;
            last_progress = clock::now();
        }
        else if (std::chrono::duration<double>(clock::now() - last_progress).count() > 2.0)
            throw std::runtime_error("the sample stream stalled (no samples for 2 s)");
    }

    window.ch1.resize(static_cast<size_t>(n));
    int rc = librador_copy_analog_window(1, start, static_cast<int>(n), window.ch1.data());
    if (rc != 0)
        throw std::runtime_error("copying CH1's samples failed (code " + std::to_string(rc) + ")");
    std::vector<double> ch2(static_cast<size_t>(n));
    rc = librador_copy_analog_window(2, start, static_cast<int>(n), ch2.data());
    if (rc == 0)
        window.ch2 = std::move(ch2);
    else if (rc != -1) // -1: CH2 does not stream in this mode
        throw std::runtime_error("copying CH2's samples failed (code " + std::to_string(rc) + ")");
    window.frames = FrameStats::now().since(before);
    return window;
}

int DeviceSession::activeTransport() const
{
    return librador_get_active_transport();
}

void DeviceSession::writeJson(JsonWriter& json) const
{
    json.field("transport", transportName(activeTransport()));
    json.key("firmware").beginObject();
    json.field("version", static_cast<int>(librador_get_device_firmware_version()));
    json.field("variant", static_cast<int>(librador_get_device_firmware_variant()));
    json.endObject();
    json.key("calibration");
    m_calibration.writeJson(json);
}

bool DeviceSession::isValidScopeGain(double gain)
{
    return std::find(SCOPE_GAINS.begin(), SCOPE_GAINS.end(), gain) != SCOPE_GAINS.end();
}

void DeviceSession::routeLogsToStderr(const bool* verbose)
{
    librador_logger_set(const_cast<bool*>(verbose), logToStderr);
}

void DeviceSession::resetLogs()
{
    librador_logger_set(nullptr, nullptr);
}
