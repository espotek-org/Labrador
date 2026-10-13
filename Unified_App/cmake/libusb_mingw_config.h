/* libusb config.h for MinGW builds of the bundled libusb (BundledLibusb.cmake).
 * Equivalent to what libusb's ./configure generates for a default
 * --host=*-w64-mingw32 static build.  MSVC builds use libusb's own
 * msvc/config.h instead. */

#define DEFAULT_VISIBILITY __attribute__ ((visibility ("default")))
#define ENABLE_LOGGING 1
#define PLATFORM_WINDOWS 1
#define PRINTF_FORMAT(a, b) __attribute__ ((__format__ (__printf__, a, b)))
