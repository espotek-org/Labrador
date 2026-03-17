/*
 * dfu-programmer
 *
 * $Id$
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */

#if HAVE_CONFIG_H
# include <config.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_LIBUSB_1_0
#include <libusb.h>
#else
#include <usb.h>
#endif

#include "config.h"
#include "dfu-device.h"
#include "dfu.h"
#include "atmel.h"
#include "arguments.h"
#include "commands.h"
#include "libdfuprog.h"

#ifdef PLATFORM_ANDROID // Exact copy of dfu_make_idle and its dependencies from dfu.c since it is declared as static
#include "util.h"

#define DFU_DETACH_TIMEOUT 1000
#define DFU_DEBUG_THRESHOLD         100
#define DEBUG(...)  dfu_debug( __FILE__, __FUNCTION__, __LINE__, \
                               DFU_DEBUG_THRESHOLD, __VA_ARGS__ )
static int32_t dfu_make_idle( dfu_device_t *device,
                              const dfu_bool initial_abort ) {
    dfu_status_t status;
    int32_t retries = 4;

    if( true == initial_abort ) {
        dfu_abort( device );
    }

    while( 0 < retries ) {
        if( 0 != dfu_get_status(device, &status) ) {
            dfu_clear_status( device );
            continue;
        }

        DEBUG( "State: %s (%d)\n", dfu_state_to_string(status.bState), status.bState );

        switch( status.bState ) {
            case STATE_DFU_IDLE:
                if( DFU_STATUS_OK == status.bStatus ) {
                    return 0;
                }

                /* We need the device to have the DFU_STATUS_OK status. */
                dfu_clear_status( device );
                break;

            case STATE_DFU_DOWNLOAD_SYNC:   /* abort -> idle */
            case STATE_DFU_DOWNLOAD_IDLE:   /* abort -> idle */
            case STATE_DFU_MANIFEST_SYNC:   /* abort -> idle */
            case STATE_DFU_UPLOAD_IDLE:     /* abort -> idle */
            case STATE_DFU_DOWNLOAD_BUSY:   /* abort -> error */
            case STATE_DFU_MANIFEST:        /* abort -> error */
                dfu_abort( device );
                break;

            case STATE_DFU_ERROR:
                dfu_clear_status( device );
                break;

            case STATE_APP_IDLE:
                dfu_detach( device, DFU_DETACH_TIMEOUT );
                break;

            case STATE_APP_DETACH:
            case STATE_DFU_MANIFEST_WAIT_RESET:
                DEBUG( "Resetting the device\n" );
                libusb_reset_device( device->handle );
                return 1;
        }

        retries--;
    }

    DEBUG( "Not able to transition the device into the dfuIDLE state.\n" );
    return -2;
}
#endif

int debug;
#ifdef HAVE_LIBUSB_1_0
libusb_context *usbcontext;
#endif

#ifdef PLATFORM_ANDROID
int dfuprog_virtual_cmd(const char *commandLine, libusb_device *device, libusb_device_handle *handle, libusb_context *parentContext, int32_t interface)
#else
int dfuprog_virtual_cmd(const char *commandLine)
#endif
{
    //Initialise variables for calculating argc.
    int argc = 2;
    const char *temp = commandLine;
    const char delimiter[] = " ";

    //Keep reading until a null terminator to find argc
    while (*temp) {
        argc += (*temp == delimiter[0]);
        temp++;
    }

    //Setup variables for argv.  We need to make a copy of the commandLine string since strtok will try to edit it (and we may not have permission!)
    char *commandLine_copy = strdup(commandLine);
    char **argv = malloc(argc * sizeof (char *));

    //Extract the arguments;
    argc = 0;
    argv[argc] = strtok(commandLine_copy, delimiter);
    while (argv[argc]) {
        fprintf(stderr, "argv[%d] = %s\n", argc, argv[argc]);
        argc++;
        argv[argc] = strtok(NULL, delimiter);
    }
    fprintf(stderr, "argc = %d\n", argc);

    fprintf(stderr, "attempting to call main()\n");
#ifdef PLATFORM_ANDROID
    int exit_code = dfuprog_virtual_main(argc, argv, device, handle, parentContext, interface);
#else
    int exit_code = dfuprog_virtual_main(argc, argv);
#endif
    free(argv);
    free(commandLine_copy);
    return exit_code;
}

#ifdef PLATFORM_ANDROID
int dfuprog_virtual_main(int argc, char **argv, libusb_device *device, libusb_device_handle *handle, libusb_context *parentContext, int32_t interface)
#else
int dfuprog_virtual_main(int argc, char **argv)
#endif
{
    static const char *progname = PACKAGE;
    int retval = SUCCESS;
    int status;
    dfu_device_t dfu_device;
    struct programmer_arguments args;
#ifndef PLATFORM_ANDROID
#ifdef HAVE_LIBUSB_1_0
    struct libusb_device *device = NULL;
#else
    struct usb_device *device = NULL;
#endif
#endif

    memset( &args, 0, sizeof(args) );
    memset( &dfu_device, 0, sizeof(dfu_device) );

    status = parse_arguments(&args, argc, argv);
    if( status < 0 ) {
        /* Exit with an error. */
        return ARGUMENT_ERROR;
    } else if (status > 0) {
        /* It was handled by parse_arguments. */
        return SUCCESS;
    }
#ifdef PLATFORM_ANDROID
    fprintf(stderr, "ARGUMENTS PARSED!!  Command = %d, VID = %d, CHIP_ID = %d, BUS_ID = %d, DEVICE_ADDRESS = %d, INITIAL_ABORT = %d, HONOR_INTERFACECLASS = %d\n", args.command, args.vendor_id, args.chip_id, args.bus_id, args.device_address, args.initial_abort, args.honor_interfaceclass);

    usbcontext = parentContext;

    dfu_device.handle = handle;
    dfu_device.interface = interface;

    int retries = 6;
    while (retries > 0) {
        switch (dfu_make_idle(&dfu_device, args.initial_abort)) {
            case 0:
                break;
            case 1:
                retries--;
        }
    }
    if (retries <= 0) {
        fprintf(stderr, "FAILED TO PUT DFU IN IDLE STATE\n");
    }
#else
#ifdef HAVE_LIBUSB_1_0
    if (libusb_init(&usbcontext)) {
        fprintf( stderr, "%s: can't init libusb.\n", progname );
        return DEVICE_ACCESS_ERROR;
    }
#else
    usb_init();
#endif

    if( debug >= 200 ) {
#ifdef HAVE_LIBUSB_1_0
        libusb_set_debug(usbcontext, debug );
#else
        usb_set_debug( debug );
#endif
    }

    if( !(args.command == com_bin2hex || args.command == com_hex2bin) ) {
        device = dfu_device_init( args.vendor_id, args.chip_id,
                                  args.bus_id, args.device_address,
                                  &dfu_device,
                                  args.initial_abort,
                                  args.honor_interfaceclass );

        if( NULL == device ) {
            fprintf( stderr, "%s: no device present.\n", progname );
            retval = DEVICE_ACCESS_ERROR;
            goto error;
        }
    }
#endif

    if( 0 != (retval = execute_command(&dfu_device, &args)) ) {
        /* command issued a specific diagnostic already */
        goto error;
    }

error:
    if( NULL != dfu_device.handle ) {
        int rv;

#ifdef HAVE_LIBUSB_1_0
        rv = libusb_release_interface( dfu_device.handle, dfu_device.interface );
#else
        rv = usb_release_interface( dfu_device.handle, dfu_device.interface );
#endif
        /* The RESET command sometimes causes the usb_release_interface command to fail.
           It is not obvious why this happens but it may be a glitch due to the hardware
           reset in the attached device. In any event, since reset causes a USB detach
           this should not matter, so there is no point in raising an alarm.
        */
        if( 0 != rv && !(com_launch == args.command &&
                args.com_launch_config.noreset == 0) ) {
            fprintf( stderr, "%s: failed to release interface %d.\n",
                             progname, dfu_device.interface );
            retval = DEVICE_ACCESS_ERROR;
        }
    }

    if( NULL != dfu_device.handle ) {
#ifdef HAVE_LIBUSB_1_0
        libusb_close(dfu_device.handle);
#else
        if( 0 != usb_close(dfu_device.handle) ) {
            fprintf( stderr, "%s: failed to close the handle.\n", progname );
            retval = DEVICE_ACCESS_ERROR;
        }
#endif
    }

#ifdef HAVE_LIBUSB_1_0
    libusb_exit(usbcontext);
#endif

    return retval;
}
