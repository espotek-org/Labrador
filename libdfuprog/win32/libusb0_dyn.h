#ifndef LIBDFUPROG_LIBUSB0_DYN_H
#define LIBDFUPROG_LIBUSB0_DYN_H

/* Nonzero once libusb0.dll (the libusb-win32 user-mode library the bootloader
 * driver package installs into System32) has been loaded and every function
 * libdfuprog needs resolved.  See libusb0_dyn.c. */
int dfuprog_libusb0_available(void);

#endif
