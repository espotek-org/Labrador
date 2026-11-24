#include "StartupSignal.h"

#include <SDL3/SDL.h>

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

static float parseNumber(const std::string& var, const std::string& value, const std::string& text)
{
    char* end = nullptr;
    errno = 0;
    const double number = std::strtod(text.c_str(), &end);
    if (text.empty() || end != text.c_str() + text.size() || errno == ERANGE
        || !std::isfinite(number) || number < 0)
        throw std::runtime_error(var + "=" + value + ": '" + text + "' is not a non-negative number");
    return (float)number;
}

std::optional<StartupSignal> startupSignalOrNone(int channel)
{
    const std::string var = "LABRADOR_SG" + std::to_string(channel);
    const char* env = SDL_getenv(var.c_str());
    if (env == nullptr)
        return std::nullopt; // the normal case: generators start off

    const std::string value(env);
    std::vector<std::string> parts;
    std::istringstream in(value);
    for (std::string part; std::getline(in, part, ':');)
        parts.push_back(part);
    if (parts.size() != 3 && parts.size() != 4)
        throw std::runtime_error(var + "=" + value + ": expected WAVE:FREQ:VPP[:BASE]");

    StartupSignal signal;
    if (parts[0] == "sine")
        signal.wave = StartupSignal::Wave::Sine;
    else if (parts[0] == "square")
        signal.wave = StartupSignal::Wave::Square;
    else if (parts[0] == "triangle")
        signal.wave = StartupSignal::Wave::Triangle;
    else if (parts[0] == "sawtooth")
        signal.wave = StartupSignal::Wave::Sawtooth;
    else
        throw std::runtime_error(var + "=" + value
            + ": WAVE must be sine, square, triangle or sawtooth");
    signal.frequency_hz = parseNumber(var, value, parts[1]);
    signal.vpp = parseNumber(var, value, parts[2]);
    signal.base_v = parts.size() == 4 ? parseNumber(var, value, parts[3]) : 0.0f;
    return signal;
}
