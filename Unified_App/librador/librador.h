#ifndef LIBRADOR_H
#define LIBRADOR_H

#include "librador_global.h"
#include "usbcallhandler.h"
#include "logging.h"
#include "librador_platform.h"
#include <vector>
#include <stdarg.h>
#include <stdint.h>

#ifdef PLATFORM_ANDROID
#include <android/log.h>
#include <jni.h>
#endif

#ifdef PLATFORM_ANDROID
#ifdef __cplusplus
extern "C" {
#endif
JNIEXPORT void JNICALL Java_com_EspoTek_Labrador_MainActivity_nativeRespondToStartupOrUsbStateChange(JNIEnv *, jobject, jboolean, jint, jboolean);
JNIEXPORT void JNICALL Java_com_EspoTek_Labrador_MainActivity_nativeInitiateFirmwareFlash(JNIEnv *, jobject);
#ifdef __cplusplus
}
#endif
#endif // PLATFORM_ANDROID

// transport_type: one of the LABRADOR_TRANSPORT_* values (usbcallhandler.h).
// AUTO resolves at connect time to the platform default: macOS bulk, 64-bit
// Windows iso6, 32-bit Windows / Linux (all) / Android iso1.  On macOS the
// iso transports are coerced to bulk unless built with
// LIBRADOR_MACOS_ALLOW_ISO: opening a full-speed iso pipe kernel-panics
// macOS Tahoe (IOUSBHostFamily NULL-dereference in getEndpointMult).
LIBRADORSHARED_EXPORT int librador_init(int transport_type = LABRADOR_TRANSPORT_AUTO);
// The transport actually in use once connected (AUTO resolved).
LIBRADORSHARED_EXPORT int librador_get_active_transport();
// Buffer-validity counters accumulated from the per-frame headers:
//   frames_ok            frames whose checksum matched
//   frames_bad_checksum  frames stomped by the ADC/DMA loop mid-flight
//                        (bulk drops these before they reach the scope
//                        buffers; iso counts them after the fact via the
//                        lag-1 meta endpoint)
//   frames_dropped       frames lost in transit (sequence-number gaps)
//   frames_unvalidated   frames received without a pairable meta record
// Any pointer may be null.  Returns -1 before librador_init.
LIBRADORSHARED_EXPORT int librador_get_frame_stats(uint64_t* frames_ok,
    uint64_t* frames_bad_checksum, uint64_t* frames_dropped, uint64_t* frames_unvalidated);
LIBRADORSHARED_EXPORT int librador_reset_frame_stats();
// On-device calibration storage (EEPROM; firmware >= 0x000A).  Values use
// the same semantics as librador_set_channel_calibration /
// librador_set_psu_calibration_offset.  A DFU chip erase wipes EEPROM, so
// re-save after a firmware flash.  Load returns 0 on success, 1 if the
// device has no valid stored calibration, <0 on transfer errors.
LIBRADORSHARED_EXPORT int librador_save_calibration_to_device(double vref_ch1,
    double gain_scale_ch1, double vref_ch2, double gain_scale_ch2, double psu_offset);
LIBRADORSHARED_EXPORT int librador_load_calibration_from_device(double* vref_ch1,
    double* gain_scale_ch1, double* vref_ch2, double* gain_scale_ch2, double* psu_offset);
LIBRADORSHARED_EXPORT int librador_exit();
LIBRADORSHARED_EXPORT int librador_reset_usb();
//Control
//a0
LIBRADORSHARED_EXPORT int librador_avr_debug();
//a1
LIBRADORSHARED_EXPORT int librador_update_signal_gen_settings(int channel, unsigned char* sampleBuffer, int numSamples, double usecs_between_samples, double amplitude_v, double offset_v);
LIBRADORSHARED_EXPORT int librador_send_sin_wave(int channel, double frequency_Hz, double amplitude_v, double offset_v, double phase_rad = 0.0);
LIBRADORSHARED_EXPORT int librador_send_square_wave(int channel, double frequency_Hz, double amplitude_v, double offset_v, double phase_rad = 0.0);
LIBRADORSHARED_EXPORT int librador_send_sawtooth_wave(int channel, double frequency_Hz, double amplitude_v, double offset_v, double phase_rad = 0.0);
LIBRADORSHARED_EXPORT int librador_send_triangle_wave(int channel, double frequency_Hz, double amplitude_v, double offset_v, double phase_rad = 0.0);
LIBRADORSHARED_EXPORT int librador_send_wave(int wf, int channel, double frequency_Hz, double amplitude_v, double offset_v);
//a2
////As above
//a3
LIBRADORSHARED_EXPORT int librador_set_power_supply_voltage(double voltage);
//a4
///As above, a1 and a2
//a5
LIBRADORSHARED_EXPORT int librador_set_device_mode(int mode);
LIBRADORSHARED_EXPORT int librador_set_oscilloscope_gain(double gain);
// Last gain applied via librador_set_oscilloscope_gain (power-on default 1).
// Returns 0 if librador_init has not run yet (0 is never a valid gain).
LIBRADORSHARED_EXPORT double librador_get_oscilloscope_gain();
// Oscilloscope calibration (Qt Desktop_Interface parity; see the Qt app's
// 3-stage calibration wizard in mainwindow.cpp).
// channel: 1 (applies to both the 375 kSps CHA buffer and the 750 kSps
//   double-rate buffer, as Qt does) or 2 (375 kSps CHB buffer).
// vref: measured channel bias voltage, Qt CalibrateVrefCHx semantics
//   (neutral 1.65).  The sample->voltage conversion adds (3.3 - vref).
// gain_scale: multiplier on the nominal R4/(R3+R4) frontend divider gain
//   (neutral 1.0).  Qt persists the absolute gain; gain_scale = that / (R4/(R3+R4)).
// Also honoured by the multimeter (mode 7) conversion, which uses the
// calibrated frontend gain but never adds vref — same as Qt's analogConvert.
// Works while disconnected, so stored calibration can be applied at startup.
LIBRADORSHARED_EXPORT int librador_set_channel_calibration(int channel, double vref, double gain_scale);
// PSU calibration offset in volts (Qt CalibratePsu semantics, neutral 0.0).
// Applied on the next librador_set_power_supply_voltage call:
// vinp = (voltage - offset)/11.  Works while disconnected.
LIBRADORSHARED_EXPORT int librador_set_psu_calibration_offset(double offset);
LIBRADORSHARED_EXPORT bool librador_get_paused(int channel);
LIBRADORSHARED_EXPORT int librador_set_paused(int channel, bool is_paused);
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

LIBRADORSHARED_EXPORT std::vector<double> * librador_get_analog_data(int channel, double timeWindow_seconds, int numToGet, double delay_seconds, int filter_mode);
// Monash-API semantics: decimate to sample_rate_hz across the window (the
// Android-era function above takes an exact sample count instead).
LIBRADORSHARED_EXPORT std::vector<double> * librador_get_analog_data_by_rate(int channel, double timeWindow_seconds, double sample_rate_hz, double delay_seconds, int filter_mode);
LIBRADORSHARED_EXPORT std::vector<double> * librador_get_analog_data_sincelast(int channel, double timeWindow_max_seconds, double sample_rate_hz, double delay_seconds, int filter_mode);
LIBRADORSHARED_EXPORT std::vector<double> * librador_get_digital_data(int channel, double timeWindow_seconds, int numToGet, double delay_seconds, bool daq = false);

LIBRADORSHARED_EXPORT int librador_daq(int channel, int numToGet, int interval_samples, usbCallHandler::daqUnitOptions units_sel[2], const char* filename);
LIBRADORSHARED_EXPORT bool librador_poll_daq_status();

LIBRADORSHARED_EXPORT std::vector<double> librador_get_time_array(double delay, double timeWindow_seconds, int n_samples);

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

LIBRADORSHARED_EXPORT void librador_set_trigger_settings(int ch, o1buffer::trigger_settings new_trigger_settings);
LIBRADORSHARED_EXPORT void librador_set_virtual_transform_settings(int ch, o1buffer::virtual_transform_settings new_virtual_transform_settings);

LIBRADORSHARED_EXPORT double librador_get_samples_per_second();
LIBRADORSHARED_EXPORT void librador_set_uart_decode_settings(int ch, UartSettings new_settings);
LIBRADORSHARED_EXPORT void librador_set_i2c_is_decoding(bool new_decode_on);
LIBRADORSHARED_EXPORT char * librador_get_uart_string(int ch, bool* parity_check);
LIBRADORSHARED_EXPORT char * librador_get_i2c_string();

LIBRADORSHARED_EXPORT int librador_init_libusb();
LIBRADORSHARED_EXPORT bool librador_iso_thread_is_active();
LIBRADORSHARED_EXPORT bool librador_is_connected();

// Jump to bootloader in preparation for a flash (fires the platform's
// connect/disconnect machinery; Android continues via USB attach events).
LIBRADORSHARED_EXPORT void librador_initiate_firmware_flash();

#ifndef PLATFORM_ANDROID
// Desktop connection management. Android drives connection through
// nativeRespondToStartupOrUsbStateChange (USB attach intents + fd injection);
// on desktop the app polls librador_device_present() and connects directly.
LIBRADORSHARED_EXPORT bool librador_device_present(bool* bootloader_mode);
// Extended presence scan (Qt Desktop_Interface Gobindar parity).  A
// "Gobindar" board is one that enumerated with PID 0xa000 instead of 0xba94
// — misconfigured/misflashed firmware.  Returns true if any Labrador-family
// device is attached (application firmware, bootloader, or Gobindar);
// *bootloader_mode / *gobindar_mode flag the two needs-recovery states.
// librador_device_present() is unchanged and never reports Gobindar boards.
LIBRADORSHARED_EXPORT bool librador_device_present_ex(bool* bootloader_mode, bool* gobindar_mode);
LIBRADORSHARED_EXPORT int librador_connect();
LIBRADORSHARED_EXPORT int librador_disconnect();
// Blocking, takes several seconds — call from a worker thread.
LIBRADORSHARED_EXPORT int librador_flash_firmware(const char* hex_path);
// Recover a board stuck in bootloader mode: launch first, reflash if needed.
// Blocking, takes several seconds — call from a worker thread.
LIBRADORSHARED_EXPORT int librador_bootloader_recover(const char* hex_path);
// Recover a Gobindar-state board (Qt deGobindarise parity).  The 0xa000
// device accepts no useful control traffic, so recovery is user-driven:
// the app must FIRST show instructions telling the user to connect Digital
// Out 1 to GND and reconnect the board (forcing the hardware bootloader),
// THEN call this.  Blocks — worker thread only — waiting up to ~10 minutes
// for the bootloader to enumerate, then runs the full erase + flash +
// launch cycle.  Returns -10 if the board never entered bootloader mode;
// other codes as librador_flash_firmware.
LIBRADORSHARED_EXPORT int librador_gobindar_recover(const char* hex_path);
// USB port reset (software replug) + disconnect; the app's poll loop
// reconnects. Clears the post-flash constant-output wedge.
LIBRADORSHARED_EXPORT int librador_hard_reset();
#endif
#endif // LIBRADOR_H
