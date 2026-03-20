#ifndef LIBDFUPROG_H
#define LIBDFUPROG_H

#ifdef PLATFORM_ANDROID
int dfuprog_virtual_cmd(const char *commandLine, libusb_device *device, libusb_device_handle *handle, libusb_context *parentContext, int32_t interface);
int dfuprog_virtual_main(int argc, char **argv, libusb_device *device, libusb_device_handle *handle, libusb_context *parentContext, int32_t interface);
#else
int dfuprog_virtual_cmd(const char *commandLine);
int dfuprog_virtual_main(int argc, char **argv);
#endif

#endif
