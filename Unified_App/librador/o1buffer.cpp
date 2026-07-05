#include "o1buffer.h"
#include "logging_internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "uartstyledecoder.h"


//TODO: incorporate mostRecentAddressPaused into getSinceLast?
//TODO: triggering is finicky when the trigger level is ~0 b/c m_triggerSensitivity becomes too small?
std::mutex buffer_mutex2;

//o1buffer is an object that has o(1) access times for its elements.
//At the moment it's basically an array, but I'm keeping it as an object so it can be changed to something more memory efficient later.
//See isobuffer in github.com/espotek-org/labrador for an example of a much more compact (RAM-wise) buffer.
o1buffer::o1buffer(double sps)
{
    buffer = (int *) (malloc(sizeof(int)*NUM_SAMPLES_PER_CHANNEL));
    buffer_paused = (int *) (malloc(sizeof(int)*NUM_SAMPLES_PER_CHANNEL));
    buffer_daq = (int *) (malloc(sizeof(int)*NUM_SAMPLES_PER_CHANNEL));
    m_is_triggered = (bool *) (malloc(sizeof(bool)*NUM_SAMPLES_PER_CHANNEL));
    m_samples_per_second = sps;
    m_uart_decoder = new uartStyleDecoder(this);
}

o1buffer::~o1buffer(){
    free(buffer);
    free(buffer_paused);
    free(m_is_triggered);
    delete m_uart_decoder;
}

int o1buffer::reset(bool hard){
    mostRecentAddress = 0;
    stream_index_at_last_call = 0;
    if(hard){
        for (int i=0; i<NUM_SAMPLES_PER_CHANNEL; i++){
            buffer[i] = 0;
        }
    }
    return 0;
}


void o1buffer::add(int value, int address){
    //Ensure that the address is not too high.
    if(address >= NUM_SAMPLES_PER_CHANNEL){
        address = address % NUM_SAMPLES_PER_CHANNEL;
    }
    if(address<0){
        LIBRADOR_LOG(LOG_ERROR, "ERROR: o1buffer::add was given a negative address\n");
    }
    //Assign the value
    buffer[address] = value;
    updateMostRecentAddress(address);
}

int o1buffer::addVector(int *firstElement, int numElements){
    int currentAddress = mostRecentAddress;

    buffer_mutex2.lock();
    for(int i=0; i< numElements; i++){
        currentAddress = (currentAddress + 1) % NUM_SAMPLES_PER_CHANNEL;
        add(firstElement[i], currentAddress);
        checkTriggered(currentAddress);
    }
    buffer_mutex2.unlock();
    return 0;
}

int o1buffer::addVector(char *firstElement, int numElements){
    int currentAddress = mostRecentAddress;

    buffer_mutex2.lock();
    for(int i=0; i< numElements; i++){
        currentAddress = (currentAddress + 1) % NUM_SAMPLES_PER_CHANNEL;
        add(firstElement[i], currentAddress);
        checkTriggered(currentAddress);
    }
    buffer_mutex2.unlock();

    return 0;
}

int o1buffer::addVector(unsigned char *firstElement, int numElements){
    int currentAddress = mostRecentAddress;

    buffer_mutex2.lock();
    for(int i=0; i< numElements; i++){
        currentAddress = (currentAddress + 1) % NUM_SAMPLES_PER_CHANNEL;
        add(firstElement[i], currentAddress);
        checkTriggered(currentAddress);
    }
    buffer_mutex2.unlock();
    return 0;
}

int o1buffer::addVector(short *firstElement, int numElements){
    int currentAddress = mostRecentAddress;

    buffer_mutex2.lock();
    for(int i=0; i< numElements; i++){
        currentAddress = (currentAddress + 1) % NUM_SAMPLES_PER_CHANNEL;
    #ifdef MULTIMETER_INVERT
        add(-firstElement[i] >> 4, currentAddress);
    #else
        add(firstElement[i] >> 4, currentAddress);
    #endif
        checkTriggered(currentAddress);
    }
    buffer_mutex2.unlock();
    return 0;
}


int o1buffer::get(int address, bool daq){
    int *read_buffer;
    if(daq) {
        read_buffer = buffer_daq;
    } else if(m_virtual_transform_settings.is_paused) {
        read_buffer = buffer_paused;
    } else {
        read_buffer = buffer;
    }

    //Ensure that the address is not too high.
    if(address >= NUM_SAMPLES_PER_CHANNEL){
        address = address % NUM_SAMPLES_PER_CHANNEL;
    }
    if(address<0){
        LIBRADOR_LOG(LOG_ERROR, "ERROR: o1buffer::get was given a negative address\n");
    }
    //Return the value
    return read_buffer[address];
}

inline void o1buffer::updateMostRecentAddress(int newAddress){
    mostRecentAddress = newAddress;
}

//This function places samples in a buffer than can be plotted on the streamingDisplay.
//A small delay, is added in case the packets arrive out of order.
std::vector<double> *o1buffer::getMany_double(int numToGet, double interval_samples, int delay_samples, int filter_mode, double scope_gain, bool twelve_bit_multimeter, bool daq){
    std::vector<double>* outvec;
    if(daq) {
        outvec = &convertedStream_double_daq;
    } else {
        outvec = &convertedStream_double;
    }
    //Resize the vector
    outvec->resize(numToGet);

    //Copy raw samples out.
    int tempAddress;
    double window_mean;

    if(m_virtual_transform_settings.is_ac)
    {
        tempAddress = (daq ? mostRecentAddressDAQ : (m_virtual_transform_settings.is_paused ? mostRecentAddressPaused : mostRecentAddress)) - delay_samples - round(interval_samples*numToGet/2.);
        if(tempAddress < 0)
            tempAddress += NUM_SAMPLES_PER_CHANNEL;
        window_mean = get_filtered_sample(tempAddress, 1, round(interval_samples * numToGet), scope_gain, twelve_bit_multimeter, daq);
        m_ac_offset_adc_units = inverseSampleConvert(window_mean + (twelve_bit_multimeter ? 0 : voltage_ref), scope_gain, twelve_bit_multimeter);
    } else {
        m_ac_offset_adc_units = 0;
    }

    for(int i=0;i<numToGet;i++){
        tempAddress = (daq ? mostRecentAddressDAQ : (m_virtual_transform_settings.is_paused ? mostRecentAddressPaused : mostRecentAddress)) - delay_samples - round(interval_samples * i);
        if(tempAddress < 0){
            tempAddress += NUM_SAMPLES_PER_CHANNEL;
        }
        double *data = outvec->data();
        if(m_virtual_transform_settings.is_ac)
            data[i] = m_virtual_transform_settings.gain * (get_filtered_sample(tempAddress, 0, round(interval_samples), scope_gain, twelve_bit_multimeter, daq) - window_mean) + m_virtual_transform_settings.offset;
        else
            data[i] = m_virtual_transform_settings.gain * get_filtered_sample(tempAddress, filter_mode, round(interval_samples), scope_gain, twelve_bit_multimeter, daq) + m_virtual_transform_settings.offset;
//         outvec->replace(i, buffer[tempAddress]);
    }
    return outvec;
}

//Reads each int as 8 bools.  Upper 3 bytes are ignored.
std::vector<double> *o1buffer::getMany_singleBit(int numToGet, double interval_subsamples, int delay_subsamples, bool daq){
    std::vector<double>* outvec;
    if(daq) {
        outvec = &convertedStream_double_daq;
    } else {
        outvec = &convertedStream_double;
    }
    //Resize the vector
    outvec->resize(numToGet);

    //Copy raw samples out.
    int tempAddress;
    int subsample_current_delay;
    uint8_t mask;
    double *data = outvec->data();
    int tempInt;

    for(int i=0;i<numToGet;i++){
        subsample_current_delay = delay_subsamples + round(interval_subsamples * i);
        tempAddress = (daq ? mostRecentAddressDAQ : (m_virtual_transform_settings.is_paused ? mostRecentAddressPaused : mostRecentAddress)) - subsample_current_delay / 8;
        mask = 0x01 << (subsample_current_delay % 8);
        if(tempAddress < 0){
            tempAddress += NUM_SAMPLES_PER_CHANNEL;
        }
        tempInt = get(tempAddress, daq);
        data[i] = (((uint8_t)tempInt) & mask) ? 1. : 0.;
        data[i] = data[i] * m_virtual_transform_settings.gain + m_virtual_transform_settings.offset;
    }
    return outvec;
}

std::vector<double> *o1buffer::getSinceLast(int feasible_window_begin, int feasible_window_end, int interval_samples, int filter_mode, double scope_gain, bool twelve_bit_multimeter){

    //Calculate what sample the feasible window begins at
    //printf_debugging("o1buffer::getSinceLast()\n")
    int feasible_start_point = mostRecentAddress - feasible_window_begin;
    if(feasible_start_point < 0){
        feasible_start_point += NUM_SAMPLES_PER_CHANNEL;
    }

    //Work out whether or not we're starting from the feasible window or the last point
    int actual_start_point;
    if(distanceFromMostRecentAddress(feasible_start_point) > distanceFromMostRecentAddress(stream_index_at_last_call + interval_samples)){
        actual_start_point = stream_index_at_last_call + interval_samples;
    } else {
        actual_start_point = feasible_start_point;
    }

    //Work out how much we're copying
    int actual_sample_distance = distanceFromMostRecentAddress(actual_start_point) - distanceFromMostRecentAddress(mostRecentAddress - feasible_window_end);
    int numToGet = actual_sample_distance/interval_samples;
    //printf_debugging("Fetching %d samples, starting at index %d with interval %d\n", numToGet, actual_start_point, interval_samples);

    //Set up the buffer
    convertedStream_double.resize(numToGet);

    //Copy raw samples out.
    int tempAddress = stream_index_at_last_call;
    for(int i=0;i<numToGet;i++){
        tempAddress = actual_start_point + (interval_samples * i);
        if(tempAddress >= NUM_SAMPLES_PER_CHANNEL){
            tempAddress -= NUM_SAMPLES_PER_CHANNEL;
        }
        double *data = convertedStream_double.data();
        if(m_virtual_transform_settings.is_ac)
            data[numToGet-1-i] = m_virtual_transform_settings.gain * (get_filtered_sample(tempAddress, 0, interval_samples, scope_gain, twelve_bit_multimeter) - get_filtered_sample(tempAddress, 1, actual_sample_distance, scope_gain, twelve_bit_multimeter)) + m_virtual_transform_settings.offset;
        else
            data[numToGet-1-i] = m_virtual_transform_settings.gain * get_filtered_sample(tempAddress, filter_mode, interval_samples, scope_gain, twelve_bit_multimeter) + m_virtual_transform_settings.offset;
        //convertedStream_double.replace(i, buffer[tempAddress]);
    }

    //update stream_index_at_last_call for next call
    stream_index_at_last_call = tempAddress;

    return &convertedStream_double;
}

int o1buffer::distanceFromMostRecentAddress(int index){
    //Standard case.  buffer[NUM_SAMPLES_PER_CHANNEL] not crossed between most recent and index's sample writes.
    if(index < mostRecentAddress){
        return mostRecentAddress - index;
    }

    //Corner case.  buffer[NUM_SAMPLES_PER_CHANNEL] boundary has been crossed.
    if(index > mostRecentAddress){
        //Two areas.  0 to mostRecentAddress, and index to the end of the buffer.
        return mostRecentAddress + (NUM_SAMPLES_PER_CHANNEL - index);
    }

    //I guess the other corner case is when the addresses are the same.
    return 0;
}

//replace with get_filtered_sample
double o1buffer::get_filtered_sample(int index, int filter_type, int filter_size, double scope_gain, bool twelve_bit_multimeter, bool daq){
    double accum = 0;
    int currentPos = index - (filter_size / 2);
    int end = currentPos + filter_size;
    int *read_buffer;
    if(daq) {
        read_buffer = buffer_daq;
    } else if(m_virtual_transform_settings.is_paused) {
        read_buffer = buffer_paused;
    } else {
        read_buffer = buffer;
    }

    switch(filter_type){
        case 0: //No filter
//             buffer[index];
            return sampleConvert(read_buffer[index], scope_gain, twelve_bit_multimeter);
        case 1: //Moving Average filter
            if(currentPos < 0){
                currentPos += NUM_SAMPLES_PER_CHANNEL;
            }
            if(end >= NUM_SAMPLES_PER_CHANNEL){
                end -= NUM_SAMPLES_PER_CHANNEL;
            }
            while(currentPos != end){
                accum += read_buffer[currentPos];
                currentPos = (currentPos + 1) % NUM_SAMPLES_PER_CHANNEL;
            }
            return sampleConvert(accum/((double)filter_size), scope_gain, twelve_bit_multimeter);
        break;
        default: //Default to "no filter"
            return (unsigned char) read_buffer[index];
    }
}

double o1buffer::sampleConvert(int sample, double scope_gain, bool twelve_bit_multimeter) const {
    double voltageLevel;
    double TOP;

    if(twelve_bit_multimeter){
        TOP = 2048;
    } else TOP = 128;

    voltageLevel = ((double)sample * (vcc/2)) / (frontendGain * scope_gain * TOP);
    if (!twelve_bit_multimeter) voltageLevel += voltage_ref;

    return voltageLevel;
}

short o1buffer::inverseSampleConvert(double voltageLevel, double scope_gain, bool twelve_bit_multimeter) const {
    double TOP;

    if(twelve_bit_multimeter){
        TOP = 2048;
    } else TOP = 128;

    if (!twelve_bit_multimeter) voltageLevel -= voltage_ref;

    short sample = round( (voltageLevel * (frontendGain * scope_gain * TOP)) / (vcc/2) );

    return sample;
}

// gets called by setsettingsforchannel in usbcallhandler.h
void o1buffer::resetTrigger(double scope_gain, bool twelve_bit_multimeter)
{
    double TOP;

    if(twelve_bit_multimeter){
        TOP = 2048;
    } else TOP = 128;

    // user sets m_trigger_settings.trigger_level based on what they see in the waveform that includes virtual transforms; these transforms are compensated for in actual_trigger_level
    double actual_trigger_level = (m_trigger_settings.trigger_level - m_virtual_transform_settings.offset)/m_virtual_transform_settings.gain;

    short new_triggerLevelADC = inverseSampleConvert(actual_trigger_level, scope_gain, twelve_bit_multimeter);
    buffer_mutex2.lock();
    memset(m_is_triggered, false, sizeof(bool) * NUM_SAMPLES_PER_CHANNEL);
    m_triggerSeekState = TriggerSeekState::Invalid;
    m_triggerLevelADC = new_triggerLevelADC;
    m_triggerSensitivity = static_cast<short>((1 + abs(actual_trigger_level * kTriggerSensitivityMultiplier )) * TOP / 128.);
    buffer_mutex2.unlock();

    LIBRADOR_LOG(LOG_DEBUG, "Trigger Level: %d", m_triggerLevelADC);
    LIBRADOR_LOG(LOG_DEBUG, "Trigger sensitivity: %d", m_triggerSensitivity);
}


void o1buffer::checkTriggered(int mostRecentAddress) {
    if (m_trigger_settings.trigger_type == TriggerType::Disabled)
        return;
    m_is_triggered[mostRecentAddress] = false;

    if (buffer[mostRecentAddress] >= ((m_triggerLevelADC + m_ac_offset_adc_units) + m_triggerSensitivity))
    {
        // Rising Edge
        if((m_triggerSeekState == TriggerSeekState::BelowTriggerLevel) && (m_trigger_settings.trigger_type == TriggerType::Rising))
            m_is_triggered[mostRecentAddress] = true;
        m_triggerSeekState = TriggerSeekState::AboveTriggerLevel;
    }
    else if (buffer[mostRecentAddress] < ((m_triggerLevelADC + m_ac_offset_adc_units) - m_triggerSensitivity))
    {
        // Falling Edge
        if((m_triggerSeekState == TriggerSeekState::AboveTriggerLevel) && (m_trigger_settings.trigger_type == TriggerType::Falling))
            m_is_triggered[mostRecentAddress] = true;
        m_triggerSeekState = TriggerSeekState::BelowTriggerLevel;
    } 
}

int o1buffer::getDelayIncludingFromTrigger(int delay_samples, int window_samples, bool daq, bool* single_shot_reached, int* trigger_delay_out) {
    int tempAddress = mostRecentAddress - delay_samples;
    if((m_trigger_settings.trigger_type == TriggerType::Disabled) || (m_virtual_transform_settings.is_paused) || daq)
        return delay_samples;
//     for (int i=0; i<(NUM_SAMPLES_PER_CHANNEL - delay_samples - window_samples); i++) {
    for (int trigger_delay=0; trigger_delay<window_samples ; trigger_delay++) {
        if(tempAddress < 0)
            tempAddress += NUM_SAMPLES_PER_CHANNEL;
        if(m_is_triggered[tempAddress])
        {
            if(m_trigger_settings.is_single_shot)
            {
                if(!(single_shot_reached == nullptr)) {
                    *single_shot_reached = true;
                }
                if(!(trigger_delay_out == nullptr)) {
                    *trigger_delay_out = trigger_delay;
                }
                setPaused(true, -trigger_delay);
                return delay_samples;
            } else {
                return delay_samples + trigger_delay;
            }
        }
        tempAddress = tempAddress - 1;
    }
    return delay_samples; // triggering enabled but no trigger point found
}

// mostRecentAddressDelta is useful for single-shot triggering: makes sure that, immediately after the single-shot trigger, getDelayIncludingFromTrigger can set trigger_delay = 0 and the trigger point will be on the rhs of the screen.  Then, the user can pan around freely while getDelayIncludingFromTrigger continues to set trigger_delay = 0.  As a side-effect, this causes the initial mostRecentAddressDelta samples to be plotted as though they occurred ~10s (or whatever the max window is) previously, but this is not a huge price to pay.  TODO : instead of using this approach, instead snap delay_samples s.t. the trigger point appears on the rhs of the screen?
int o1buffer::setPaused(bool is_paused, int mostRecentAddressDelta, bool hard){
    if(is_paused && (!m_virtual_transform_settings.is_paused || hard)) {
        buffer_mutex2.lock();
        m_virtual_transform_settings.is_paused = is_paused;
        memcpy(buffer_paused, buffer, sizeof(int)*NUM_SAMPLES_PER_CHANNEL);
        mostRecentAddressPaused = mostRecentAddress + mostRecentAddressDelta;
        buffer_mutex2.unlock();
    }
    return 0;
}

void o1buffer::copy_to_daq(){
    // caller should hold a mutex protection on 'buffer' access
    mostRecentAddressDAQ = mostRecentAddress;
    memcpy(buffer_daq, buffer, sizeof(int)*NUM_SAMPLES_PER_CHANNEL);
}

bool o1buffer::getPaused(){
    return m_virtual_transform_settings.is_paused;
}

bool o1buffer::setTriggerSettings(o1buffer::trigger_settings new_trigger_settings){
    bool update_trigger = 
         !(m_trigger_settings.trigger_type == new_trigger_settings.trigger_type) ||
         !(m_trigger_settings.trigger_level == new_trigger_settings.trigger_level);
    m_trigger_settings = new_trigger_settings;
    return update_trigger;
}

bool o1buffer::setVirtualTransformSettings(o1buffer::virtual_transform_settings new_virtual_transform_settings){
    bool update_trigger =  // purposely omitting is_paused
         !(m_virtual_transform_settings.offset == new_virtual_transform_settings.offset) ||
         !(m_virtual_transform_settings.gain == new_virtual_transform_settings.gain) ||
         !(m_virtual_transform_settings.is_ac == new_virtual_transform_settings.is_ac);
    setPaused(new_virtual_transform_settings.is_paused);
    m_virtual_transform_settings = new_virtual_transform_settings;
    return update_trigger;
}

bool o1buffer::isTriggeringEnabled() {
    return !(m_trigger_settings.trigger_type == o1buffer::TriggerType::Disabled);
}

void o1buffer::UartDecode()
{
    m_uart_decoder->UartDecode();
}

void o1buffer::setUartDecodeSettings(UartSettings new_settings)
{
    m_uart_decoder->setSettings(new_settings);
}

char * o1buffer::getUart_String(bool* parity_check)
{
    return m_uart_decoder->getString(parity_check);
}
