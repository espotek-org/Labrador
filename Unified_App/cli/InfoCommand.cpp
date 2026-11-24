#include "InfoCommand.h"

#include "DeviceSession.h"
#include "JsonWriter.h"
#include "OptionParser.h"

#include "librador.h"

#include <cstdio>
#include <iostream>

namespace
{
struct Options
{
    bool json = false;
    bool verbose = false;
};

OptionParser makeParser(Options& options)
{
    OptionParser parser;
    parser.addFlag("--json", "print the report as JSON", [&]() { options.json = true; });
    parser.addFlag("--verbose", "also show librador's debug messages on stderr",
                   [&]() { options.verbose = true; });
    return parser;
}

std::string version(unsigned version, unsigned variant)
{
    char text[48];
    std::snprintf(text, sizeof text, "0x%04x variant %u", version, variant);
    return text;
}

// What a board in its application firmware reports.
struct AppReport
{
    unsigned version = 0;
    unsigned variant = 0;
    bool current = false;
    int transport = 0;
    BoardCalibration calibration; // read only when the firmware is current
};
} // namespace

std::string InfoCommand::name() const
{
    return "info";
}

std::string InfoCommand::summary() const
{
    return "Show the board's firmware, USB transport and stored calibration";
}

void InfoCommand::printHelp(std::ostream& out, const std::string& program) const
{
    Options unused;
    out << program << " info - " << summary() << "\n\n"
        << "Reports whether a board is attached and in which state (running its\n"
        << "firmware, waiting in bootloader mode, or misconfigured), the firmware\n"
        << "version against the one this build needs (" << version(EXPECTED_FIRMWARE_VERSION,
                                                              DEFINED_EXPECTED_VARIANT)
        << "),\nthe USB transport in use and the calibration stored on the board. It reads\n"
        << "only and changes nothing.\n\n"
        << "Usage:\n"
        << "  " << program << " info [options]\n\n"
        << "Options:\n";
    makeParser(unused).printOptions(out);
    out << "\nExit status is 0 when a board was found, whatever its state.\n";
}

int InfoCommand::run(const std::vector<std::string>& args)
{
    Options options;
    makeParser(options).parse(args);

    LibradorScope librador(&options.verbose, UsbTransport::Auto);
    bool bootloader = false;
    bool gobindar = false;
    if (!librador_device_present_ex(&bootloader, &gobindar))
        throw CommandError("no Labrador board found; is it plugged in?");

    const char* state = gobindar ? "gobindar" : bootloader ? "bootloader" : "application";
    AppReport app;
    if (!gobindar && !bootloader)
    {
        const int rc = librador_connect();
        if (rc < 0)
            throw CommandError("could not open the board (code " + std::to_string(rc) +
                               "); is it open in another program?");
        app.version = librador_get_device_firmware_version();
        app.variant = librador_get_device_firmware_variant();
        app.current = rc == 0;
        app.transport = librador_get_active_transport();
        // Calibration storage needs firmware 0x000A or newer; only the current
        // firmware is known to answer the request.
        if (app.current)
            app.calibration = DeviceSession::readCalibration();
    }
    const bool is_app = !gobindar && !bootloader;

    if (options.json)
    {
        JsonWriter json(std::cout);
        json.beginObject();
        json.field("command", "info");
        json.field("state", state);
        json.key("expected_firmware").beginObject();
        json.field("version", static_cast<int>(EXPECTED_FIRMWARE_VERSION));
        json.field("variant", static_cast<int>(DEFINED_EXPECTED_VARIANT));
        json.endObject();
        json.key("firmware");
        if (is_app)
        {
            json.beginObject();
            json.field("version", static_cast<int>(app.version));
            json.field("variant", static_cast<int>(app.variant));
            json.endObject();
        }
        else
            json.null();
        json.field("firmware_current", is_app && app.current);
        json.key("transport");
        if (is_app)
            json.value(transportName(app.transport));
        else
            json.null();
        json.key("calibration");
        if (is_app && app.current)
            app.calibration.writeJson(json);
        else
            json.null();
        json.endObject();
        json.finish();
        return EXIT_SUCCESS;
    }

    if (gobindar)
        std::cout << "board: misconfigured (USB product id 0xa000); the Labrador app's "
                     "recovery dialog repairs it\n";
    else if (bootloader)
        std::cout << "board: in bootloader mode; 'labrador-cli update-firmware' launches "
                     "or reflashes it\n";
    else
    {
        std::cout << "board: firmware " << version(app.version, app.variant)
                  << (app.current ? " (current)"
                                  : " (this build needs " +
                                        version(EXPECTED_FIRMWARE_VERSION,
                                                DEFINED_EXPECTED_VARIANT) +
                                        "; run 'labrador-cli update-firmware')")
                  << '\n'
                  << "USB transport: " << transportName(app.transport) << '\n';
        if (!app.current)
            std::cout << "calibration: not read (needs the current firmware)\n";
        else if (!app.calibration.stored)
            std::cout << "calibration: none stored on the board\n";
        else
        {
            char text[200];
            std::snprintf(text, sizeof text,
                          "calibration: CH1 vref %.4f V gain x%.4f, CH2 vref %.4f V gain "
                          "x%.4f, PSU offset %.4f V\n",
                          app.calibration.vref_ch1, app.calibration.gain_scale_ch1,
                          app.calibration.vref_ch2, app.calibration.gain_scale_ch2,
                          app.calibration.psu_offset_v);
            std::cout << text;
        }
    }
    return EXIT_SUCCESS;
}
