/*
 * libusb-win32 (libusb-0.1 API) forwarders, resolved from libusb0.dll at run
 * time.
 *
 * Why: on Windows the Atmel DFU bootloader (03EB:2FE4) is bound to the
 * libusb-win32 kernel driver (libusb0.sys) by Bootloader_Install.  Driving
 * that driver through libusb-1.0 is unreliable: about 2% of control
 * transfers come back with a bogus length (measured on hardware: 62 and 83
 * bad results in two runs of 3000 DFU_GETSTATUS requests with libusb 1.0.29,
 * 0 in 6000 through libusb0.dll).  A bad length on a write aborts the flash
 * half-way; on a read it is handed to memcpy and crashes the process
 * (espotek-org/Labrador#450).  libusb0.dll is the API that driver was written
 * for, and what the standalone dfu-programmer.exe has always used.
 *
 * Why run-time loading: libusb0.dll is installed into System32 by the driver
 * package, not shipped with the app.  Importing it at link time would stop
 * the app from starting on a machine without the driver; this way the app
 * runs and only a firmware flash reports the missing driver.
 *
 * dfu-programmer is built without HAVE_LIBUSB_1_0 on Windows (see config.h),
 * so its usb_*() calls land on the functions below.
 */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "usb.h"
#include "libusb0_dyn.h"

static HMODULE libusb0;
static int load_attempted;

static usb_dev_handle *(*p_usb_open)(struct usb_device *);
static int (*p_usb_close)(usb_dev_handle *);
static int (*p_usb_control_msg)(usb_dev_handle *, int, int, int, int, char *, int, int);
static int (*p_usb_set_configuration)(usb_dev_handle *, int);
static int (*p_usb_claim_interface)(usb_dev_handle *, int);
static int (*p_usb_release_interface)(usb_dev_handle *, int);
static int (*p_usb_reset)(usb_dev_handle *);
static void (*p_usb_init)(void);
static void (*p_usb_set_debug)(int);
static int (*p_usb_find_busses)(void);
static int (*p_usb_find_devices)(void);
static struct usb_bus *(*p_usb_get_busses)(void);

#define RESOLVE(name) \
    do { \
        p_##name = (void *)GetProcAddress(libusb0, #name); \
        if (!p_##name) \
            complete = 0; \
    } while (0)

int dfuprog_libusb0_available(void)
{
    int complete = 1;

    if (load_attempted)
        return libusb0 != NULL;
    load_attempted = 1;

    /* Load by full path from the system directory (SysWOW64 for the 32-bit
     * build, via WOW64 redirection): that is where the driver package
     * installs it, and it keeps a stray libusb0.dll on the DLL search path
     * from being picked up.  (A full path rather than
     * LOAD_LIBRARY_SEARCH_SYSTEM32, which older toolchain headers and
     * unpatched Windows 7 lack.) */
    {
        char path[MAX_PATH + 16];
        UINT len = GetSystemDirectoryA(path, MAX_PATH);
        if (len > 0 && len < MAX_PATH) {
            strcpy(path + len, "\\libusb0.dll");
            libusb0 = LoadLibraryA(path);
        }
    }
    if (!libusb0) {
        fprintf(stderr, "libdfuprog: libusb0.dll not found (error %lu). The firmware-update "
                        "(bootloader) USB driver is not installed.\n", GetLastError());
        return 0;
    }

    RESOLVE(usb_open);
    RESOLVE(usb_close);
    RESOLVE(usb_control_msg);
    RESOLVE(usb_set_configuration);
    RESOLVE(usb_claim_interface);
    RESOLVE(usb_release_interface);
    RESOLVE(usb_reset);
    RESOLVE(usb_init);
    RESOLVE(usb_set_debug);
    RESOLVE(usb_find_busses);
    RESOLVE(usb_find_devices);
    RESOLVE(usb_get_busses);
    if (!complete) {
        fprintf(stderr, "libdfuprog: libusb0.dll is missing expected exports\n");
        FreeLibrary(libusb0);
        libusb0 = NULL;
        return 0;
    }
    return 1;
}

/* Every forwarder degrades to "no device / error" when the DLL is absent. */

void usb_init(void)
{
    if (dfuprog_libusb0_available())
        p_usb_init();
}

void usb_set_debug(int level)
{
    if (dfuprog_libusb0_available())
        p_usb_set_debug(level);
}

int usb_find_busses(void)
{
    return dfuprog_libusb0_available() ? p_usb_find_busses() : -1;
}

int usb_find_devices(void)
{
    return dfuprog_libusb0_available() ? p_usb_find_devices() : -1;
}

struct usb_bus *usb_get_busses(void)
{
    return dfuprog_libusb0_available() ? p_usb_get_busses() : NULL;
}

usb_dev_handle *usb_open(struct usb_device *dev)
{
    return dfuprog_libusb0_available() ? p_usb_open(dev) : NULL;
}

int usb_close(usb_dev_handle *dev)
{
    return dfuprog_libusb0_available() ? p_usb_close(dev) : -1;
}

int usb_control_msg(usb_dev_handle *dev, int requesttype, int request,
                    int value, int index, char *bytes, int size, int timeout)
{
    return dfuprog_libusb0_available()
        ? p_usb_control_msg(dev, requesttype, request, value, index, bytes, size, timeout)
        : -1;
}

int usb_set_configuration(usb_dev_handle *dev, int configuration)
{
    return dfuprog_libusb0_available() ? p_usb_set_configuration(dev, configuration) : -1;
}

int usb_claim_interface(usb_dev_handle *dev, int interface)
{
    return dfuprog_libusb0_available() ? p_usb_claim_interface(dev, interface) : -1;
}

int usb_release_interface(usb_dev_handle *dev, int interface)
{
    return dfuprog_libusb0_available() ? p_usb_release_interface(dev, interface) : -1;
}

int usb_reset(usb_dev_handle *dev)
{
    return dfuprog_libusb0_available() ? p_usb_reset(dev) : -1;
}
