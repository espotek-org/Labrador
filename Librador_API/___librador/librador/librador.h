#ifndef LIBRADOR_H
#define LIBRADOR_H

#include "librador_global.h"
#include "logging.h"
#include <vector>
#include <stdarg.h>
#include <stdint.h>

LIBRADORSHARED_EXPORT int librador_init();
LIBRADORSHARED_EXPORT int librador_exit();
LIBRADORSHARED_EXPORT int librador_setup_usb();
LIBRADORSHARED_EXPORT int librador_setup_usb_control();
LIBRADORSHARED_EXPORT int librador_setup_usb_iso();
LIBRADORSHARED_EXPORT int librador_reset_usb();
//Control
//a0
LIBRADORSHARED_EXPORT int librador_avr_debug();
//a1
LIBRADORSHARED_EXPORT int librador_update_signal_gen_settings(int channel, unsigned char* sampleBuffer, int numSamples, double usecs_between_samples, double amplitude_v, double offset_v);
LIBRADORSHARED_EXPORT int librador_send_sin_wave(int channel, double frequency_Hz, double amplitude_v, double offset_v);
LIBRADORSHARED_EXPORT int librador_send_square_wave(int channel, double frequency_Hz, double amplitude_v, double offset_v);
LIBRADORSHARED_EXPORT int librador_send_sawtooth_wave(int channel, double frequency_Hz, double amplitude_v, double offset_v);
LIBRADORSHARED_EXPORT int librador_send_triangle_wave(int channel, double frequency_Hz, double amplitude_v, double offset_v);
//a2
////As above
//a3
LIBRADORSHARED_EXPORT int librador_set_power_supply_voltage(double voltage);
//a4
///As above, a1 and a2
//a5
LIBRADORSHARED_EXPORT int librador_set_device_mode(int mode);
LIBRADORSHARED_EXPORT int librador_set_oscilloscope_gain(double gain);
//a6
LIBRADORSHARED_EXPORT int librador_set_digital_out(int channel, bool state_on);
//a7
LIBRADORSHARED_EXPORT int librador_reset_device();
LIBRADORSHARED_EXPORT int librador_jump_to_bootloader();
//a8
LIBRADORSHARED_EXPORT uint16_t librador_get_device_firmware_version();
//a9
LIBRADORSHARED_EXPORT uint8_t librador_get_device_firmware_variant();
//aa
//LIBRADORSHARED_EXPORT int librador_kickstart_isochronous_loop();

LIBRADORSHARED_EXPORT std::vector<double> * librador_get_analog_data(int channel, double timeWindow_seconds, double sample_rate_hz, double delay_seconds, int filter_mode);
LIBRADORSHARED_EXPORT std::vector<double> * librador_get_analog_data_sincelast(int channel, double timeWindow_max_seconds, double sample_rate_hz, double delay_seconds, int filter_mode);
LIBRADORSHARED_EXPORT std::vector<uint8_t> * librador_get_digital_data(int channel, double timeWindow_seconds, double sample_rate_hz, double delay_seconds);

//TODO: flashFirmware();


/*
 * Should never be unsynchronised...  Hide these ones
LIBRADORSHARED_EXPORT int librador_synchronise_begin();
LIBRADORSHARED_EXPORT int librador_synchronise_end();
*/

typedef void (*librador_logger_p)(void * userdata, const int level, const char * format, va_list);

LIBRADORSHARED_EXPORT void librador_logger_set(void * userdata, librador_logger_p logger);
LIBRADORSHARED_EXPORT librador_logger_p librador_logger_get(void);
LIBRADORSHARED_EXPORT void * librador_logger_get_userdata(void);


#endif // LIBRADOR_H
