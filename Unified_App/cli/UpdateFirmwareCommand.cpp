#include "UpdateFirmwareCommand.h"

#include "DeviceSession.h"
#include "OptionParser.h"

#include "librador.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

namespace
{
struct Options
{
    std::string hex; // empty = the hex bundled next to the binary
    bool force = false;
    bool verbose = false;
};

std::string expectedFirmwareName()
{
    char name[48];
    std::snprintf(name, sizeof name, "labrafirm_%04X_%02u.hex",
                  static_cast<unsigned>(EXPECTED_FIRMWARE_VERSION),
                  static_cast<unsigned>(DEFINED_EXPECTED_VARIANT));
    return name;
}

std::string describeVersion(unsigned version, unsigned variant)
{
    char text[48];
    std::snprintf(text, sizeof text, "0x%04x variant %u", version, variant);
    return text;
}

std::string boardVersion()
{
    return describeVersion(librador_get_device_firmware_version(),
                           librador_get_device_firmware_variant());
}

std::string expectedVersion()
{
    return describeVersion(EXPECTED_FIRMWARE_VERSION, DEFINED_EXPECTED_VARIANT);
}

OptionParser makeParser(Options& options)
{
    OptionParser parser;
    parser.addOption("--hex", "FILE",
                     "Intel hex image to flash; default is the bundled " +
                         expectedFirmwareName(),
                     [&](const std::string& v) { options.hex = v; });
    parser.addFlag("--force", "reflash even if the board already runs the expected firmware",
                   [&]() { options.force = true; });
    parser.addFlag("--verbose", "also show librador's debug messages on stderr",
                   [&]() { options.verbose = true; });
    return parser;
}

// The same place the Labrador app keeps its firmware: assets/firmware next to
// the executable (Contents/Resources inside a macOS bundle).
std::string bundledHexPath()
{
    const char* base = SDL_GetBasePath();
    if (base == nullptr)
        throw CommandError(std::string("cannot locate the executable directory: ") +
                           SDL_GetError());
    return std::string(base) + "assets/firmware/" + expectedFirmwareName();
}

// RAII around librador so every exit path tears the USB session down.
class LibradorScope
{
  public:
    explicit LibradorScope(const bool* verbose)
    {
        DeviceSession::routeLogsToStderr(verbose);
        if (librador_init(LABRADOR_TRANSPORT_AUTO) < 0)
        {
            DeviceSession::resetLogs();
            throw CommandError("could not initialise librador");
        }
    }
    ~LibradorScope()
    {
        librador_exit();
        DeviceSession::resetLogs();
    }
    LibradorScope(const LibradorScope&) = delete;
    LibradorScope& operator=(const LibradorScope&) = delete;
};
} // namespace

std::string UpdateFirmwareCommand::name() const
{
    return "update-firmware";
}

std::string UpdateFirmwareCommand::summary() const
{
    return "Flash the board with the firmware this build expects";
}

void UpdateFirmwareCommand::printHelp(std::ostream& out, const std::string& program) const
{
    Options unused;
    out << program << " update-firmware - " << summary() << "\n\n"
        << "Checks the attached board's firmware against the version librador streams\n"
        << "from (" << expectedVersion() << ") and reflashes it when they differ. A board\n"
        << "stuck in bootloader mode is launched first and reflashed only if that fails.\n"
        << "Flashing takes about ten seconds; do not unplug the board meanwhile.\n\n"
        << "Usage:\n"
        << "  " << program << " update-firmware [options]\n\n"
        << "Options:\n";
    makeParser(unused).printOptions(out);
    out << "\nExit status is 0 when the board ends up on the expected firmware, whether\n"
        << "or not a flash was needed.\n";
}

int UpdateFirmwareCommand::run(const std::vector<std::string>& args)
{
    Options options;
    makeParser(options).parse(args);

    const std::string hex = options.hex.empty() ? bundledHexPath() : options.hex;
    // Checked before any USB work so a missing image fails without touching the board.
    if (!std::ifstream(hex, std::ios::binary))
        throw CommandError("cannot read firmware image '" + hex + "'" +
                           (options.hex.empty() ? "; pass --hex FILE" : ""));

    LibradorScope librador(&options.verbose);

    bool bootloader = false;
    bool gobindar = false;
    if (!librador_device_present_ex(&bootloader, &gobindar))
        throw CommandError("no Labrador board found; is it plugged in?");
    if (gobindar)
        throw CommandError("the board enumerates in its misconfigured (Gobindar) state, "
                           "which needs the manual recovery in the Labrador app");

    int rc;
    if (bootloader)
    {
        std::cerr << "board is in bootloader mode: launching its firmware, reflashing "
                     "from " << hex << " if that fails" << std::endl;
        rc = librador_bootloader_recover(hex.c_str());
        if (rc == 1 && !options.force)
        {
            // Launched, but into a firmware librador will not stream from.
            std::cerr << "launched firmware " << boardVersion() << ", expected "
                      << expectedVersion() << ": reflashing" << std::endl;
            rc = librador_flash_firmware(hex.c_str());
        }
    }
    else
    {
        rc = librador_connect();
        if (rc < 0)
            throw CommandError("could not open the board (code " + std::to_string(rc) +
                               "); is it open in another program?");
        const std::string current = boardVersion();
        if (rc == 0 && !options.force)
        {
            std::cout << "firmware " << current << " is already current; use --force to reflash"
                      << std::endl;
            return EXIT_SUCCESS;
        }
        std::cerr << "board reports " << current << ", flashing " << expectedVersion()
                  << " from " << hex << " (about ten seconds, do not unplug)" << std::endl;
        rc = librador_flash_firmware(hex.c_str());
    }

    if (rc < 0)
        throw CommandError("flashing failed (code " + std::to_string(rc) +
                           "); the board may be left in bootloader mode, in which case "
                           "running this command again recovers it");
    if (rc == 1)
        throw CommandError("flashing finished but the board reports " + boardVersion() +
                           ", expected " + expectedVersion());
    std::cout << "done: board now runs " << boardVersion() << std::endl;
    return EXIT_SUCCESS;
}
