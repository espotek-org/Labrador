#include "librador.h"
#include "librador_internal.h"
#include "logging_internal.h"

#define _USE_MATH_DEFINES

#include <vector>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>

#ifdef PLATFORM_ANDROID
JNIEXPORT void JNICALL Java_com_EspoTek_Labrador_MainActivity_nativeRespondToStartupOrUsbStateChange(JNIEnv *env, jobject thisobject, jboolean is_plugged_in, jint file_descriptor, jboolean bootloader_mode)
{
    if(!internal_librador_object)
    {
        librador_init();
    }
    internal_librador_object->usb_driver->respondToStartupOrUsbStateChange((bool) is_plugged_in, (int) file_descriptor, (bool) bootloader_mode);
    return;
}

JNIEXPORT void JNICALL Java_com_EspoTek_Labrador_MainActivity_nativeInitiateFirmwareFlash(JNIEnv *env, jobject thisobject)
{
    internal_librador_object->usb_driver->initiateFirmwareFlash();
    return;
}
#endif

Librador::Librador()
{
    usb_driver = new usbCallHandler(LABRADOR_VID, LABRADOR_PID);
}

int librador_init(int transport_type){
    if(internal_librador_object){
        //Object already initialised; still honour a transport change ahead
        //of the next connect.
        internal_librador_object->usb_driver->set_transport(transport_type);
        return 1;
    }

    internal_librador_object = new Librador();
    if(!internal_librador_object){
        //Object initialisation failed
        return -1;
    }
    if(internal_librador_object->usb_driver->set_transport(transport_type) < 0){
        return -2;
    }
    //good, fresh initialisation
    return 0;
}

int librador_get_active_transport(){
    CHECK_API_INITIALISED
    return internal_librador_object->usb_driver->get_active_transport();
}

int librador_get_frame_stats(uint64_t* frames_ok, uint64_t* frames_bad_checksum,
        uint64_t* frames_dropped, uint64_t* frames_unvalidated){
    CHECK_API_INITIALISED
    internal_librador_object->usb_driver->get_frame_stats(frames_ok,
        frames_bad_checksum, frames_dropped, frames_unvalidated);
    return 0;
}

int librador_reset_frame_stats(){
    CHECK_API_INITIALISED
    internal_librador_object->usb_driver->reset_frame_stats();
    return 0;
}

int librador_save_calibration_to_device(double vref_ch1, double gain_scale_ch1,
        double vref_ch2, double gain_scale_ch2, double psu_offset){
    CHECK_API_INITIALISED
    return internal_librador_object->usb_driver->save_calibration_to_device(
        vref_ch1, gain_scale_ch1, vref_ch2, gain_scale_ch2, psu_offset);
}

int librador_load_calibration_from_device(double* vref_ch1, double* gain_scale_ch1,
        double* vref_ch2, double* gain_scale_ch2, double* psu_offset){
    CHECK_API_INITIALISED
    return internal_librador_object->usb_driver->load_calibration_from_device(
        vref_ch1, gain_scale_ch1, vref_ch2, gain_scale_ch2, psu_offset);
}

int librador_exit(){
    CHECK_API_INITIALISED
    if(!internal_librador_object){
        //Object not yet initialised
        return 1;
    }

    delete internal_librador_object;
    internal_librador_object = nullptr;
    //Object deleted
    return 0;
}

int librador_avr_debug(){
    CHECK_API_INITIALISED
    CHECK_USB_INITIALISED
    return internal_librador_object->usb_driver->avrDebug();
}

int librador_daq(int channel, int numToGet, int interval_samples, usbCallHandler::daqUnitOptions units_sel[2], const char* filename)
{
    CHECK_API_INITIALISED
    CHECK_USB_INITIALISED

    internal_librador_object->usb_driver->spawn_daq_thread(channel, numToGet, interval_samples, units_sel, filename);
    return 1;
}

double librador_get_samples_per_second()
{
    return internal_librador_object->usb_driver->get_samples_per_second();
}

std::vector<double> * librador_get_analog_data(int channel, double timeWindow_seconds, int numToGet, double delay_seconds, int filter_mode)
{
    VECTOR_API_INIT_CHECK
    VECTOR_USB_INIT_CHECK

    double samples_per_second = internal_librador_object->usb_driver->get_samples_per_second();

    if(samples_per_second == 0){
        return nullptr;
    }

    double interval_samples = timeWindow_seconds * samples_per_second / (numToGet-1);
    int delay_samples = round(delay_seconds * samples_per_second);
//     int numToGet = round(timeWindow_seconds * samples_per_second)/interval_samples;

    return internal_librador_object->usb_driver->getMany_double(channel, numToGet, interval_samples, delay_samples, filter_mode);
}

std::vector<double> * librador_get_analog_data_by_rate(int channel, double timeWindow_seconds, double sample_rate_hz, double delay_seconds, int filter_mode)
{
    VECTOR_API_INIT_CHECK
    VECTOR_USB_INIT_CHECK

    double samples_per_second = internal_librador_object->usb_driver->get_samples_per_second();
    if(samples_per_second == 0){
        return nullptr;
    }

    // Exact math of the Monash-era librador_get_analog_data
    int interval_samples = round(samples_per_second / sample_rate_hz);
    int delay_samples = round(delay_seconds * samples_per_second);
    int numToGet = round(timeWindow_seconds * samples_per_second)/interval_samples;
    return internal_librador_object->usb_driver->getMany_double(channel, numToGet, interval_samples, delay_samples, filter_mode);
}

// int librador_setTriggerType(int channel, o1buffer::TriggerType trigger_type)
// {
//     CHECK_API_INITIALISED
//     return internal_librador_object->usb_driver->setTriggerType(channel, trigger_type);
// }

std::vector<double> librador_get_time_array(double delay, double timeWindow_seconds, int n_samples) {
    std::vector<double> time_array(n_samples,0);
    double sample_period = timeWindow_seconds / (n_samples - 1);
    double* data = time_array.data();
    for(int i=0; i < n_samples; i++) {
        data[i] = -delay - i * sample_period;
    }
    return time_array;
}


std::vector<double> * librador_get_digital_data(int channel, double timeWindow_seconds, int numToGet, double delay_seconds, bool daq){
    VECTOR_API_INIT_CHECK
    VECTOR_USB_INIT_CHECK

    double subsamples_per_second = internal_librador_object->usb_driver->get_samples_per_second() * 8;

    if(subsamples_per_second == 0){
        return nullptr;
    }

    double interval_subsamples = timeWindow_seconds * subsamples_per_second / (numToGet-1);
    int delay_subsamples = round(delay_seconds * subsamples_per_second);
//     int numToGet = round(timeWindow_seconds * subsamples_per_second)/interval_subsamples;

    LIBRADOR_LOG(LOG_DEBUG, "interval_subsamples = %f\ndelay_subsamples = %d\nnumToGet=%d\n", interval_subsamples, delay_subsamples, numToGet);

    return internal_librador_object->usb_driver->getMany_singleBit(channel, numToGet, interval_subsamples, delay_subsamples);
}


std::vector<double> * librador_get_analog_data_sincelast(int channel, double timeWindow_max_seconds, double sample_rate_hz, double delay_seconds, int filter_mode)
{
    VECTOR_API_INIT_CHECK
    VECTOR_USB_INIT_CHECK

    double samples_per_second = internal_librador_object->usb_driver->get_samples_per_second();

    if(samples_per_second == 0){
        return nullptr;
    }

    int interval_samples = round(samples_per_second / sample_rate_hz);
    int feasible_window_end = round(delay_seconds * samples_per_second);
    int feasible_window_begin = round((delay_seconds + timeWindow_max_seconds) * samples_per_second);

    return internal_librador_object->usb_driver->getMany_sincelast(channel, feasible_window_begin, feasible_window_end, interval_samples, filter_mode);

}

int librador_update_signal_gen_settings(int channel, unsigned char *sampleBuffer, int numSamples, double usecs_between_samples, double amplitude_v, double offset_v){
    CHECK_API_INITIALISED
    CHECK_USB_INITIALISED
    int error = internal_librador_object->usb_driver->update_function_gen_settings(channel, sampleBuffer, numSamples, usecs_between_samples, amplitude_v, offset_v);
    if(error){
        return error-1000;
    } else return internal_librador_object->usb_driver->send_function_gen_settings(channel);
}

int librador_set_power_supply_voltage(double voltage){
    CHECK_API_INITIALISED
    CHECK_USB_INITIALISED
    return internal_librador_object->usb_driver->set_psu_voltage(voltage);
}

int librador_set_device_mode(int mode){
    CHECK_API_INITIALISED
    CHECK_USB_INITIALISED
    return internal_librador_object->usb_driver->set_device_mode(mode);
}

int librador_set_oscilloscope_gain(double gain){
    CHECK_API_INITIALISED
    CHECK_USB_INITIALISED
    return internal_librador_object->usb_driver->set_gain(gain);
}

double librador_get_oscilloscope_gain(){
    if(!internal_librador_object){
        return 0; // API not initialised; 0 is never a valid gain
    }
    return internal_librador_object->usb_driver->get_scope_gain();
}

int librador_set_channel_calibration(int channel, double vref, double gain_scale){
    CHECK_API_INITIALISED
    // Deliberately no CHECK_USB_INITIALISED: calibration lives in the
    // conversion buffers, which exist independent of the USB link, so stored
    // values can be restored at startup before the board connects.
    return internal_librador_object->usb_driver->set_channel_calibration(channel, vref, gain_scale);
}

int librador_set_psu_calibration_offset(double offset){
    CHECK_API_INITIALISED
    // No CHECK_USB_INITIALISED: the offset is stored and applied on the next
    // librador_set_power_supply_voltage call, so it can be restored before
    // the board connects.
    return internal_librador_object->usb_driver->set_psu_calibration_offset(offset);
}

int librador_set_paused(int channel, bool is_paused){
    CHECK_API_INITIALISED
    return internal_librador_object->usb_driver->setPaused(channel, is_paused);
}

bool librador_get_paused(int channel){
    CHECK_API_INITIALISED
    return internal_librador_object->usb_driver->getPaused(channel);
}

int librador_set_digital_out(int channel, bool state_on){
    CHECK_API_INITIALISED
    CHECK_USB_INITIALISED
    static uint8_t channelStates[4] = {0, 0, 0, 0};
    channel--;
    if((channel < 0) || (channel > 3)){
        return -1000; //Invalid Channel
    }
    channelStates[channel] = state_on ? 1 : 0;

    return internal_librador_object->usb_driver->set_digital_state((channelStates [0] | channelStates[1] << 1 | channelStates[2] << 2 | channelStates[3] << 3));
}

int librador_reset_device(){
    CHECK_API_INITIALISED
    CHECK_USB_INITIALISED
    return internal_librador_object->usb_driver->reset_device(false);
}

int librador_jump_to_bootloader(){
    CHECK_API_INITIALISED
    CHECK_USB_INITIALISED
    return internal_librador_object->usb_driver->reset_device(true);
}

uint16_t librador_get_device_firmware_version(){
    CHECK_API_INITIALISED
    CHECK_USB_INITIALISED
    return internal_librador_object->usb_driver->get_firmware_version();
}

uint8_t librador_get_device_firmware_variant(){
    CHECK_API_INITIALISED
    CHECK_USB_INITIALISED
    return internal_librador_object->usb_driver->get_firmware_variant();
}

int round_to_log2(double in){
    //Round down to the nearest power of 2.
    return round(pow(2, floor(log2(in))));
}

unsigned char generator_sin(double x)
{
    //Offset of 1 and divided by 2 shifts range from -1:1 to 0:1.  We've got to return an unsigned char, after all!
    return (unsigned char)round(255.0 * ((sin(x)+1)/2));
}

unsigned char generator_square(double x)
{
    return (x > M_PI) ? 255 : 0;
}

unsigned char generator_sawtooth(double x)
{
    return round(255.0 * (x/(2.0*M_PI)));
}

unsigned char generator_triangle(double x)
{
    if(x <= M_PI){
        return round(255.0 * (x/M_PI));
    } else {
        return round(255.0 * (1 -((x - M_PI)/M_PI)));
    }
}

int send_convenience_waveform(int channel, double frequency_Hz, double amplitude_v, double offset_v, unsigned char (*sample_generator)(double), double phase_rad = 0.0)
{
    if((amplitude_v + offset_v) > 9.6){
        return -1;
        //Voltage range too high
    }
    if((amplitude_v < 0) | (offset_v < 0)){
        return -2;
        //Negative voltage
    }

    if((channel != 1) && (channel != 2)){
        return -3;
        //Invalid channel
    }
    int num_samples = fmin(1000000.0/frequency_Hz, 512);
    //The maximum number of samples that Labrador's buffer holds is 512.
    //The minimum time between samples is 1us.  Using T=1/f, this gives a maximum sample number of 10^6/f.
    num_samples = 2*(num_samples / 2);
    //Square waves need an even number.  Others don't care.
    double usecs_between_samples = 1000000.0/((double)num_samples * frequency_Hz);
    //Again, from T=1/f.
    unsigned char* sampleBuffer = (unsigned char*)malloc(num_samples);

    int i;
    double x_temp;
    for(i=0; i< num_samples; i++){
        x_temp = (double)i * (2.0*M_PI/(double)num_samples);
        //Generate points at interval 2*pi/num_samples.
        sampleBuffer[i] = sample_generator(x_temp - phase_rad);
    }

    librador_update_signal_gen_settings(channel, sampleBuffer, num_samples, usecs_between_samples, amplitude_v, offset_v);

    free(sampleBuffer);
    return 0;
}

int librador_send_sin_wave(int channel, double frequency_Hz, double amplitude_v, double offset_v, double phase_rad){
    CHECK_API_INITIALISED
    CHECK_USB_INITIALISED
    return send_convenience_waveform(channel, frequency_Hz, amplitude_v, offset_v, generator_sin, phase_rad);
}

int librador_send_square_wave(int channel, double frequency_Hz, double amplitude_v, double offset_v, double phase_rad){
    CHECK_API_INITIALISED
    CHECK_USB_INITIALISED
    return send_convenience_waveform(channel, frequency_Hz, amplitude_v, offset_v, generator_square, phase_rad);
}

int librador_send_triangle_wave(int channel, double frequency_Hz, double amplitude_v, double offset_v, double phase_rad){
    CHECK_API_INITIALISED
    CHECK_USB_INITIALISED
    return send_convenience_waveform(channel, frequency_Hz, amplitude_v, offset_v, generator_triangle, phase_rad);
}

int librador_send_sawtooth_wave(int channel, double frequency_Hz, double amplitude_v, double offset_v, double phase_rad){
    CHECK_API_INITIALISED
    CHECK_USB_INITIALISED
    return send_convenience_waveform(channel, frequency_Hz, amplitude_v, offset_v, generator_sawtooth, phase_rad);
}

int librador_send_wave(int wf, int channel, double frequency_Hz, double amplitude_v, double offset_v){
    CHECK_API_INITIALISED
    CHECK_USB_INITIALISED
    switch(wf)
    {
        case 0:
            return send_convenience_waveform(channel, frequency_Hz, amplitude_v, offset_v, generator_sin);
        case 1:
            return send_convenience_waveform(channel, frequency_Hz, amplitude_v, offset_v, generator_square);
        case 2:
            return send_convenience_waveform(channel, frequency_Hz, amplitude_v, offset_v, generator_triangle);
        case 3:
            return send_convenience_waveform(channel, frequency_Hz, amplitude_v, offset_v, generator_sawtooth);
    };
    return -4;
}



/*
int librador_synchronise_begin(){
    CHECK_API_INITIALISED
    return internal_librador_object->usb_driver->set_synchronous_pause_state(true);
}

int librador_synchronise_end(){
    CHECK_API_INITIALISED
    return internal_librador_object->usb_driver->set_synchronous_pause_state(false);
}
*/

static void std_logger(void * userdata, const int level, const char * format, va_list ap);
static librador_logger_p _librador_global_logger = std_logger;
static void * _librador_global_userdata = nullptr;

void librador_global_logger(const int level, const char * format, ...){
	va_list args;
	va_start(args, format);
	if (_librador_global_logger)
		_librador_global_logger(_librador_global_userdata, level, format, args);
	va_end(args);
}

void librador_logger_set(void * userdata, librador_logger_p logger){
	_librador_global_logger = logger ? logger : std_logger;
	_librador_global_userdata = userdata;
}

librador_logger_p librador_logger_get(void){
	return _librador_global_logger;
}

void * librador_logger_get_userdata(void){
	return _librador_global_userdata;
}

static void std_logger(void * userdata, const int level, const char * format, va_list ap){
	vfprintf((level > LOG_ERROR) ?  stdout : stderr , format, ap);
}

void librador_set_trigger_settings(int channel, o1buffer::trigger_settings new_trigger_settings)
{
    return internal_librador_object->usb_driver->setTriggerSettings(channel, new_trigger_settings);
}

void librador_set_virtual_transform_settings(int channel, o1buffer::virtual_transform_settings new_virtual_transform_settings)
{
    return internal_librador_object->usb_driver->setVirtualTransformSettings(channel, new_virtual_transform_settings);
}
 
char * librador_get_uart_string(int channel, bool* parity_check)
{
    return internal_librador_object->usb_driver->getUart_String(channel, parity_check);
}

void librador_set_uart_decode_settings(int ch, UartSettings new_settings) {
    internal_librador_object->usb_driver->setUartDecodeSettings(ch, new_settings);
}

char * librador_get_i2c_string()
{
    return internal_librador_object->usb_driver->getI2c_String();
}

void librador_set_i2c_is_decoding(bool new_decode_on)
{
    internal_librador_object->usb_driver->setI2cIsDecoding(new_decode_on);
}

bool librador_is_connected()
{
    if(!internal_librador_object) return false;
    return internal_librador_object->usb_driver->connected;
}

int librador_reset_usb()
{
    // Declared in the legacy API but left undefined in the Android copy.
    // Tear down the connection; reconnection is app-driven (desktop poll
    // loop / Android USB attach intents).
    CHECK_API_INITIALISED
    internal_librador_object->usb_driver->teardown_connection();
    return 0;
}

static librador_host_hooks _librador_host_hooks;

void librador_set_host_hooks(const librador_host_hooks& hooks)
{
    _librador_host_hooks = hooks;
}

const librador_host_hooks& librador_get_host_hooks()
{
    return _librador_host_hooks;
}

void librador_initiate_firmware_flash()
{
    internal_librador_object->usb_driver->initiateFirmwareFlash();
}

#ifndef PLATFORM_ANDROID
bool librador_device_present(bool* bootloader_mode)
{
    if(!internal_librador_object) return false;
    return internal_librador_object->usb_driver->desktop_device_present(bootloader_mode);
}

bool librador_device_present_ex(bool* bootloader_mode, bool* gobindar_mode)
{
    *bootloader_mode = false;
    *gobindar_mode = false;
    if(!internal_librador_object) return false;
    bool app_mode = false;
    return internal_librador_object->usb_driver->desktop_device_present_ex(&app_mode, bootloader_mode, gobindar_mode);
}

int librador_connect()
{
    CHECK_API_INITIALISED
    return internal_librador_object->usb_driver->desktop_connect();
}

int librador_disconnect()
{
    CHECK_API_INITIALISED
    internal_librador_object->usb_driver->desktop_disconnect();
    return 0;
}

int librador_flash_firmware(const char* hex_path)
{
    CHECK_API_INITIALISED
    return internal_librador_object->usb_driver->desktop_flash_firmware(hex_path);
}

int librador_bootloader_recover(const char* hex_path)
{
    CHECK_API_INITIALISED
    return internal_librador_object->usb_driver->desktop_bootloader_recover(hex_path);
}

int librador_gobindar_recover(const char* hex_path)
{
    CHECK_API_INITIALISED
    return internal_librador_object->usb_driver->desktop_gobindar_recover(hex_path);
}

int librador_hard_reset()
{
    CHECK_API_INITIALISED
    return internal_librador_object->usb_driver->desktop_hard_reset();
}
#endif

bool librador_iso_thread_is_active()
{
    return internal_librador_object->usb_driver->isoThreadIsActive();
}

bool librador_poll_daq_status()
{
    return internal_librador_object->usb_driver->poll_daq_status();
}


