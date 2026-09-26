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
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif
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

/* Desktop device-discovery retry (see libdfuprog.h).  Windows binds the
 * libusb-win32 driver to the bootloader device asynchronously after it
 * re-enumerates, so the first dfu-programmer call right after the board jumps
 * to the bootloader can find the device but not open it yet. */
#ifdef _WIN32
#define DFUPROG_DEFAULT_DEVICE_RETRIES 20
#else
#define DFUPROG_DEFAULT_DEVICE_RETRIES 0
#endif
#define DFUPROG_DEFAULT_RETRY_INTERVAL_MS 250

static int device_retry_attempts = DFUPROG_DEFAULT_DEVICE_RETRIES;
static int device_retry_interval_ms = DFUPROG_DEFAULT_RETRY_INTERVAL_MS;

void dfuprog_set_device_retry(int attempts, int interval_ms)
{
    device_retry_attempts = (attempts < 0) ? 0 : attempts;
    device_retry_interval_ms = (interval_ms < 0) ? 0 : interval_ms;
}

#ifndef PLATFORM_ANDROID
static void dfuprog_sleep_ms(int ms)
{
    if (ms <= 0)
        return;
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    {
        struct timespec ts;
        ts.tv_sec = ms / 1000;
        ts.tv_nsec = (long)(ms % 1000) * 1000000L;
        nanosleep(&ts, NULL);
    }
#endif
}
#endif /* !PLATFORM_ANDROID */

void dfuprog_free_args(dfuprog_args *args)
{
    if (!args)
        return;
    free(args->argv);
    free(args->storage);
    args->argv = NULL;
    args->storage = NULL;
    args->argc = 0;
}

static int dfuprog_is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

int dfuprog_split_args(const char *command_line, dfuprog_args *out)
{
    const char *p;
    char *w;
    int argc = 0;
    int capacity = 8;

    if (!out)
        return -1;
    out->argc = 0;
    out->argv = NULL;
    out->storage = NULL;
    if (!command_line)
        return -1;

    /* Every token is a substring of the input with quotes dropped, so one
     * buffer of the input's size holds all of them (NUL-terminated). */
    out->storage = (char *)malloc(strlen(command_line) + 1);
    out->argv = (char **)malloc((size_t)capacity * sizeof(char *));
    if (!out->storage || !out->argv) {
        dfuprog_free_args(out);
        return -1;
    }

    p = command_line;
    w = out->storage;
    while (*p) {
        int in_quotes = 0;
        char *token;

        while (dfuprog_is_space(*p))
            p++;
        if (!*p)
            break;

        token = w;
        while (*p && (in_quotes || !dfuprog_is_space(*p))) {
            if (*p == '"') {        /* quotes delimit, they are not data */
                in_quotes = !in_quotes;
                p++;
                continue;
            }
            *w++ = *p++;
        }
        *w++ = '\0';

        if (argc + 1 >= capacity) {   /* keep room for the NULL terminator */
            char **grown;
            capacity *= 2;
            grown = (char **)realloc(out->argv, (size_t)capacity * sizeof(char *));
            if (!grown) {
                dfuprog_free_args(out);
                return -1;
            }
            out->argv = grown;
        }
        out->argv[argc++] = token;
    }
    out->argv[argc] = NULL;
    out->argc = argc;
    return argc;
}

#ifdef PLATFORM_ANDROID
int dfuprog_virtual_cmd(const char *commandLine, libusb_device *device, libusb_device_handle *handle, libusb_context *parentContext, int32_t interface)
#else
int dfuprog_virtual_cmd(const char *commandLine)
#endif
{
    dfuprog_args args;
    int i;
    int exit_code;

    /* Quote-aware split: a hex path such as
     * "C:\Program Files\EspoTek Labrador\assets\firmware\labrafirm_000C_03.hex"
     * stays one argument when the caller wraps it in double quotes.  (The old
     * strtok(" ") split broke every path containing a space.) */
    if (dfuprog_split_args(commandLine, &args) < 0) {
        fprintf(stderr, "dfuprog_virtual_cmd: could not parse the command line\n");
        return UNSPECIFIED_ERROR;
    }

    for (i = 0; i < args.argc; i++) {
        fprintf(stderr, "argv[%d] = %s\n", i, args.argv[i]);
    }
    fprintf(stderr, "argc = %d\n", args.argc);

    fprintf(stderr, "attempting to call main()\n");
#ifdef PLATFORM_ANDROID
    exit_code = dfuprog_virtual_main(args.argc, args.argv, device, handle, parentContext, interface);
#else
    exit_code = dfuprog_virtual_main(args.argc, args.argv);
#endif
    dfuprog_free_args(&args);
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
    debug = 0;  /* per call, like a fresh dfu-programmer process; --debug sets it */

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
    dfu_bool made_idle = false;
    while (retries > 0 && made_idle == false) {
        switch (dfu_make_idle(&dfu_device, args.initial_abort)) {
            case 0:
                made_idle = true;
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
        int attempt = 0;
        for(;;) {
            device = dfu_device_init( args.vendor_id, args.chip_id,
                                      args.bus_id, args.device_address,
                                      &dfu_device,
                                      args.initial_abort,
                                      args.honor_interfaceclass );
            if( NULL != device || attempt >= device_retry_attempts )
                break;
            /* Not found, or found but not yet openable (Windows is still
             * binding the driver to the freshly enumerated bootloader). */
            attempt++;
            fprintf( stderr, "%s: DFU device not ready, retry %d/%d in %d ms\n",
                     progname, attempt, device_retry_attempts, device_retry_interval_ms );
            dfuprog_sleep_ms( device_retry_interval_ms );
        }

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
