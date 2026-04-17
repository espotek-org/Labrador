#ifndef LIBRADOR_INTERNAL_H
#define LIBRADOR_INTERNAL_H

#endif // LIBRADOR_INTERNAL_H

#define LABRADOR_VID 0x03eb
#define LABRADOR_PID 0xba94

#define CHECK_API_INITIALISED if(!internal_librador_object) return -420;
#define CHECK_USB_INITIALISED if(!internal_librador_object->usb_driver->connected) return -421;

#define VECTOR_API_INIT_CHECK if(!internal_librador_object) return nullptr;
#define VECTOR_USB_INIT_CHECK if(!internal_librador_object->usb_driver->connected) return nullptr;


class usbCallHandler;

class Librador
{

public:
    Librador();
    usbCallHandler *usb_driver = nullptr;
};

Librador *internal_librador_object = nullptr;
