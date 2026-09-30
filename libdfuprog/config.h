/* Windows desktop builds use dfu-programmer's libusb-0.1 code path, served by
 * libusb0.dll (see win32/libusb0_dyn.c): libusb-1.0 is unreliable on the
 * libusb-win32 kernel driver the bootloader is bound to.  Every other
 * platform uses libusb-1.0. */
#ifndef _WIN32
#define HAVE_LIBUSB_1_0 1
#endif
#define PACKAGE "dfu-programmer"
#define PACKAGE_BUGREPORT ""
#define PACKAGE_NAME "dfu-programmer"
#define PACKAGE_STRING "dfu-programmer 0.7.2"
#define PACKAGE_TARNAME "dfu-programmer"
#define PACKAGE_URL "https://github.com/dfu-programmer/dfu-programmer"
#define PACKAGE_VERSION "0.7.2"
#define VERSION "0.7.2"
