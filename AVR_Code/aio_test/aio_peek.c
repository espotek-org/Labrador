// Peek at the AIO firmware debug state (vendor req 0xab) without claiming
// any interface, so it can observe a running app's streaming state.
#include <stdio.h>
#include <unistd.h>
#include <libusb.h>

int main(void) {
    libusb_context *ctx;
    if (libusb_init(&ctx)) { fprintf(stderr, "init failed\n"); return 1; }
    libusb_device_handle *h = libusb_open_device_with_vid_pid(ctx, 0x03eb, 0xba94);
    if (!h) { fprintf(stderr, "device not found\n"); return 1; }
    unsigned char d[8];
    for (int i = 0; i < 3; i++) {
        int r = libusb_control_transfer(h, 0xC0, 0xab, 0, 0, d, 8, 1000);
        if (r != 8) { fprintf(stderr, "0xab failed: %d\n", r); return 1; }
        printf("transport=%u armfail=0x%02x iso_cb=%u meta_cb=%u bulkhdr_cb=%u bulkpay_cb=%u usb_state=%u mode=%u\n",
               d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7]);
        usleep(500000);
    }
    libusb_close(h);
    libusb_exit(ctx);
    return 0;
}
