#include "Signal.h"

#include "JsonWriter.h"

#include "librador.h"

#include <sstream>
#include <stdexcept>
#include <vector>

namespace
{
std::string formatNumber(double value)
{
    std::ostringstream text;
    text << value;
    return text.str();
}
} // namespace

const char* waveName(SignalSpec::Wave wave)
{
    switch (wave)
    {
    case SignalSpec::Wave::Off: return "off";
    case SignalSpec::Wave::Dc: return "dc";
    case SignalSpec::Wave::Sine: return "sine";
    case SignalSpec::Wave::Square: return "square";
    case SignalSpec::Wave::Triangle: return "triangle";
    case SignalSpec::Wave::Sawtooth: return "sawtooth";
    }
    throw std::logic_error("unknown wave");
}

double SignalSpec::peakV() const
{
    switch (wave)
    {
    case Wave::Off: return 0;
    case Wave::Dc: return base_v;
    default: return base_v + vpp;
    }
}

std::string SignalSpec::text() const
{
    switch (wave)
    {
    case Wave::Off: return "off";
    case Wave::Dc: return "dc:" + formatNumber(base_v);
    default:
        return std::string(waveName(wave)) + ":" + formatNumber(frequency_hz) + ":" +
               formatNumber(vpp) + ":" + formatNumber(base_v);
    }
}

void applyPsu(double volts)
{
    const int rc = librador_set_power_supply_voltage(volts);
    if (rc != 0)
        throw std::runtime_error("the board rejected PSU voltage " + formatNumber(volts) +
                                 " V (code " + std::to_string(rc) + ")");
}

SignalOutput applySignal(int channel, const SignalSpec& spec)
{
    int rc;
    switch (spec.wave)
    {
    case SignalSpec::Wave::Off:
        // What the app's generator "off" sends (GenericSignal::turnOff).
        rc = librador_send_sin_wave(channel, 100, 0.0, 0.0);
        break;
    case SignalSpec::Wave::Dc:
    {
        std::vector<unsigned char> flat(16, 0);
        rc = librador_update_signal_gen_settings(channel, flat.data(),
                                                 static_cast<int>(flat.size()), 10.0, 0.0,
                                                 spec.base_v);
        break;
    }
    case SignalSpec::Wave::Sine:
        rc = librador_send_sin_wave(channel, spec.frequency_hz, spec.vpp, spec.base_v);
        break;
    case SignalSpec::Wave::Square:
        rc = librador_send_square_wave(channel, spec.frequency_hz, spec.vpp, spec.base_v);
        break;
    case SignalSpec::Wave::Triangle:
        rc = librador_send_triangle_wave(channel, spec.frequency_hz, spec.vpp, spec.base_v);
        break;
    case SignalSpec::Wave::Sawtooth:
        rc = librador_send_sawtooth_wave(channel, spec.frequency_hz, spec.vpp, spec.base_v);
        break;
    default:
        throw std::logic_error("unknown wave");
    }
    if (rc != 0)
        throw std::runtime_error("the board rejected '" + spec.text() + "' on SG" +
                                 std::to_string(channel) + " (code " + std::to_string(rc) + ")");

    SignalOutput out;
    rc = librador_get_signal_gen_output(channel, &out.frequency_hz, &out.v_min, &out.v_max);
    if (rc != 0)
        throw std::runtime_error("cannot read back SG" + std::to_string(channel) +
                                 "'s setting (code " + std::to_string(rc) + ")");
    if (!spec.isPeriodic())
        out.frequency_hz = 0;
    return out;
}

void writeSignalJson(JsonWriter& json, const SignalSpec& spec, const SignalOutput& out)
{
    json.beginObject();
    json.field("spec", spec.text());
    json.field("wave", waveName(spec.wave));
    if (spec.isPeriodic())
    {
        json.field("frequency_hz", spec.frequency_hz);
        json.field("vpp", spec.vpp);
    }
    if (spec.wave != SignalSpec::Wave::Off)
        json.field("base_v", spec.base_v);
    json.key("actual").beginObject();
    if (spec.isPeriodic())
        json.field("frequency_hz", out.frequency_hz);
    json.field("v_min", out.v_min);
    json.field("v_max", out.v_max);
    json.endObject();
    json.endObject();
}
