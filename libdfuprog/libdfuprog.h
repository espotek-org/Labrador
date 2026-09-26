#ifndef LIBDFUPROG_H
#define LIBDFUPROG_H

/*
 * libdfuprog - dfu-programmer compiled into the host application.
 *
 * Callers include this header inside an `extern "C"` block (see
 * usbcallhandler.h); libusb.h must be included before it on Android because
 * the Android entry points take libusb handles.
 */

/* A command line split into a NULL-terminated argv (see dfuprog_split_args). */
typedef struct {
    int argc;
    char **argv;    /* argv[argc] == NULL */
    char *storage;  /* backing store for the argv strings; owned by the struct */
} dfuprog_args;

/* Split `command_line` into arguments the way a shell would for the simple
 * cases dfu-programmer needs: arguments are separated by spaces/tabs and a
 * double-quoted run ("...") is a single argument with the quotes removed, so a
 * path containing spaces (e.g. C:\Program Files\EspoTek Labrador\...) can be
 * passed as "C:\Program Files\...".  Backslashes are literal (no escapes), so
 * Windows paths pass through untouched.  Returns argc (>= 0) and fills *out,
 * or -1 on allocation failure.  Release with dfuprog_free_args(). */
int dfuprog_split_args(const char *command_line, dfuprog_args *out);
void dfuprog_free_args(dfuprog_args *args);

/* Desktop only: when dfu-programmer cannot find/open the DFU device, retry
 * discovery `attempts` more times, sleeping `interval_ms` between attempts.
 * Windows needs this: right after the board re-enumerates in bootloader mode
 * the OS is still binding the libusb-win32 driver to it, so the first
 * dfu-programmer call can race that and report "no device present".
 * Defaults: 20 x 250 ms on Windows, no retries elsewhere. */
void dfuprog_set_device_retry(int attempts, int interval_ms);

#ifdef PLATFORM_ANDROID
int dfuprog_virtual_cmd(const char *commandLine, libusb_device *device, libusb_device_handle *handle, libusb_context *parentContext, int32_t interface);
int dfuprog_virtual_main(int argc, char **argv, libusb_device *device, libusb_device_handle *handle, libusb_context *parentContext, int32_t interface);
#else
int dfuprog_virtual_cmd(const char *commandLine);
int dfuprog_virtual_main(int argc, char **argv);
#endif

#endif
