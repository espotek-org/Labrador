#include "librador.h"
#include "librador_internal.h"
#include "usbcallhandler.h"
#include "logging_internal.h"

#define _USE_MATH_DEFINES

#include <vector>
#include <math.h>

Librador::Librador()
{
    usb_driver = new usbCallHandler(LABRADOR_VID, LABRADOR_PID);
}

int librador_init(){
    if(internal_librador_object != NULL){
        //Object already initialised
        return 1;
    }

    internal_librador_object = new Librador();
    if(internal_librador_object == NULL){
        //Object initialisation failed
        return -1;
    } else {
        //good, fresh initialisation
        return 0;
    }
}

int librador_exit(){
    CHECK_API_INITIALISED
    if(internal_librador_object == NULL){
        //Object not yet initialised
        return 1;
    }

    delete internal_librador_object;
    internal_librador_object = NULL;
    //Object deleted
    return 0;
}

int librador_setup_usb(){
    CHECK_API_INITIALISED
    int error;
    //Setup USB for Control (EP0) transfers.
    error = internal_librador_object->usb_driver->setup_usb_control();
    if(error < 0){
        return error;
    }
    //Setup USB for Isochronous transfers.
    error = internal_librador_object->usb_driver->setup_usb_iso();
    if(error < 0){
        return error - 1000;
    }
    return 0;
}

int librador_avr_debug(){
    CHECK_API_INITIALISED
    CHECK_USB_INITIALISED
    return internal_librador_object->usb_driver->avrDebug();
}

std::vector<double> * librador_get_analog_data(int channel, double timeWindow_seconds, double sample_rate_hz, double delay_seconds, int filter_mode){
    VECTOR_API_INIT_CHECK
    VECTOR_USB_INIT_CHECK

    double samples_per_second = internal_librador_object->usb_driver->get_samples_per_second();

    if(samples_per_second == 0){
        return NULL;
    }


    int interval_samples = round(samples_per_second / sample_rate_hz);
    int delay_samples = round(delay_seconds * samples_per_second);
    int numToGet = round(timeWindow_seconds * samples_per_second)/interval_samples;

    return internal_librador_object->usb_driver->getMany_double(channel, numToGet, interval_samples, delay_samples, filter_mode);
}

std::vector<uint8_t> * librador_get_digital_data(int channel, double timeWindow_seconds, double sample_rate_hz, double delay_seconds){
    VECTOR_API_INIT_CHECK
    VECTOR_USB_INIT_CHECK

    double subsamples_per_second = internal_librador_object->usb_driver->get_samples_per_second() * 8;

    if(subsamples_per_second == 0){
        return NULL;
    }


    int interval_subsamples = round(subsamples_per_second / sample_rate_hz);
    int delay_subsamples = round(delay_seconds * subsamples_per_second);
    int numToGet = round(timeWindow_seconds * subsamples_per_second)/interval_subsamples;

    LIBRADOR_LOG(LOG_DEBUG, "interval_subsamples = %d\ndelay_subsamples = %d\nnumToGet=%d\n", interval_subsamples, delay_subsamples, numToGet);

    return internal_librador_object->usb_driver->getMany_singleBit(channel, numToGet, interval_subsamples, delay_subsamples);
}


std::vector<double> * librador_get_analog_data_sincelast(int channel, double timeWindow_max_seconds, double sample_rate_hz, double delay_seconds, int filter_mode){

    VECTOR_API_INIT_CHECK
    VECTOR_USB_INIT_CHECK

    double samples_per_second = internal_librador_object->usb_driver->get_samples_per_second();

    if(samples_per_second == 0){
        return NULL;
    }

    int interval_samples = round(samples_per_second / sample_rate_hz);
    int feasible_window_end = round(delay_seconds * samples_per_second);
    int feasible_window_begin = round((delay_seconds + timeWindow_max_seconds) * samples_per_second);

    return internal_librador_object->usb_driver->getMany_sincelast(channel, feasible_window_begin, feasible_window_end, interval_samples, filter_mode);

}

std::vector<uint8_t> * librador_get_digital_data_sincelast(int channel, double timeWindow_max_seconds, double sample_rate_hz, double delay_seconds){
    VECTOR_API_INIT_CHECK
    VECTOR_USB_INIT_CHECK

    double subsamples_per_second = internal_librador_object->usb_driver->get_samples_per_second() * 8;

    if(subsamples_per_second == 0){
        return NULL;
    }


    int interval_subsamples = round(subsamples_per_second / sample_rate_hz);
    int feasible_window_end = round(delay_seconds * subsamples_per_second);
    int feasible_window_begin = round((delay_seconds + timeWindow_max_seconds) * subsamples_per_second);

    printf("interval_subsamples = %d\nfeasible_window_end = %d\nfeasible_window_begin = %d\n", interval_subsamples, feasible_window_end, feasible_window_begin);

    return internal_librador_object->usb_driver->getMany_singleBit_sincelast(channel, feasible_window_begin, feasible_window_end, interval_subsamples);
}


int librador_reset_usb(){
    CHECK_API_INITIALISED
    CHECK_USB_INITIALISED
    delete internal_librador_object->usb_driver;
    internal_librador_object->usb_driver = new usbCallHandler(LABRADOR_VID, LABRADOR_PID);
    return 0;
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

int send_convenience_waveform(int channel, double frequency_Hz, double amplitude_v, double offset_v, unsigned char (*sample_generator)(double))
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
        sampleBuffer[i] = sample_generator(x_temp);
    }

    librador_update_signal_gen_settings(channel, sampleBuffer, num_samples, usecs_between_samples, amplitude_v, offset_v);

    free(sampleBuffer);
    return 0;
}

int librador_send_sin_wave(int channel, double frequency_Hz, double amplitude_v, double offset_v){
    CHECK_API_INITIALISED
    CHECK_USB_INITIALISED
    return send_convenience_waveform(channel, frequency_Hz, amplitude_v, offset_v, generator_sin);
}

int librador_send_square_wave(int channel, double frequency_Hz, double amplitude_v, double offset_v){
    CHECK_API_INITIALISED
    CHECK_USB_INITIALISED
    return send_convenience_waveform(channel, frequency_Hz, amplitude_v, offset_v, generator_square);
}

int librador_send_triangle_wave(int channel, double frequency_Hz, double amplitude_v, double offset_v){
    CHECK_API_INITIALISED
    CHECK_USB_INITIALISED
    return send_convenience_waveform(channel, frequency_Hz, amplitude_v, offset_v, generator_triangle);
}

int librador_send_sawtooth_wave(int channel, double frequency_Hz, double amplitude_v, double offset_v){
    CHECK_API_INITIALISED
    CHECK_USB_INITIALISED
    return send_convenience_waveform(channel, frequency_Hz, amplitude_v, offset_v, generator_sawtooth);
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
static void * _librador_global_userdata = NULL;

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
