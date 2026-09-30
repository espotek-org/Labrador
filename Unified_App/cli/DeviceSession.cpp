#include "DeviceSession.h"

#include "Command.h"

#include "librador.h"

#include <array>
#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <thread>

namespace
{
constexpr std::array<double, 8> SCOPE_GAINS = {0.5, 1, 2, 4, 8, 16, 32, 64};

// librador device modes: 0 = CH1 scope, 2 = CH1 + CH2 scope.
constexpr int DEVICE_MODE_CH1 = 0;
constexpr int DEVICE_MODE_CH1_CH2 = 2;

void logToStderr(void* userdata, const int level, const char* format, va_list args)
{
    const bool verbose = *static_cast<const bool*>(userdata);
    if (level == LOG_DEBUG && !verbose)
        return;
    std::vfprintf(stderr, format, args);
}
} // namespace

DeviceSession::DeviceSession(bool verbose) : m_verbose(verbose)
{
    librador_logger_set(&m_verbose, logToStderr);

    if (librador_init(LABRADOR_TRANSPORT_AUTO) < 0)
    {
        librador_logger_set(nullptr, nullptr);
        throw CommandError("could not initialise librador");
    }
    const int rc = librador_connect();
    if (rc < 0 || !librador_is_connected())
    {
        librador_exit();
        librador_logger_set(nullptr, nullptr);
        throw CommandError("could not connect to a Labrador board (code " +
                           std::to_string(rc) +
                           "); is it plugged in and not open in another program?");
    }
}

DeviceSession::~DeviceSession()
{
    librador_exit();
    librador_logger_set(nullptr, nullptr);
}

void DeviceSession::configureScope(ScopeMode mode, double gain, double settle_s)
{
    if (librador_set_oscilloscope_gain(gain) < 0)
        throw CommandError("the board rejected gain " + std::to_string(gain));
    librador_set_device_mode(mode == ScopeMode::Ch1 ? DEVICE_MODE_CH1
                                                     : DEVICE_MODE_CH1_CH2);
    std::this_thread::sleep_for(std::chrono::duration<double>(settle_s));
}

bool DeviceSession::isValidScopeGain(double gain)
{
    return std::find(SCOPE_GAINS.begin(), SCOPE_GAINS.end(), gain) != SCOPE_GAINS.end();
}
