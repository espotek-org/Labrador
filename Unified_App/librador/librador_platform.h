#ifndef LIBRADOR_PLATFORM_H
#define LIBRADOR_PLATFORM_H

struct SDL_IOStream;

// Hooks the host application registers so the library can reach platform/UI
// facilities (dialogs, media scanning, Android asset extraction) without
// linking against them. Pointers must stay valid for the process lifetime.
// Null fields keep the built-in default (log-only, or plain-file I/O).
struct librador_host_hooks
{
    // Firmware version/variant mismatch detected on connect. Host should ask
    // the user, then call librador_initiate_firmware_flash() (Android) or
    // librador_flash_firmware() (desktop).
    void (*request_firmware_flash)() = nullptr;

    // Firmware flash finished and the board relaunched into the new firmware.
    void (*confirm_firmware_flash)() = nullptr;

    // Whether an upcoming bootloader-mode enumeration is expected (Android
    // sets a field on MainActivity so the attach handler doesn't warn).
    void (*set_bootloader_mode_allowed)(bool allowed) = nullptr;

    // A DAQ file has been fully written (Android: trigger a media scan).
    void (*daq_file_written)(const char* filepath) = nullptr;

    // Open a writable stream for DAQ output.
    // Default: SDL_IOFromFile(filepath, "w"). Android routes through a
    // content:// URI obtained from MainActivity.
    SDL_IOStream* (*open_daq_stream)(const char* filepath) = nullptr;

    // Resolve a firmware image name (e.g. "labrafirm_0007_02.hex") to a path
    // on the filesystem. Android copies it out of the APK assets first.
    // Returned pointer must stay valid until the next call.
    const char* (*prepare_firmware_hex)(const char* filename) = nullptr;
};

void librador_set_host_hooks(const librador_host_hooks& hooks);
const librador_host_hooks& librador_get_host_hooks();

#endif // LIBRADOR_PLATFORM_H
