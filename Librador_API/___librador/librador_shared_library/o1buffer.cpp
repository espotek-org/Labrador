#include "o1buffer.h"
#include "logging_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>


//o1buffer is an object that has o(1) access times for its elements.
//At the moment it's basically an array, but I'm keeping it as an object so it can be changed to something more memory efficient later.
//See isobuffer in github.com/espotek-org/labrador for an example of a much more compact (RAM-wise) buffer.
o1buffer::o1buffer()
{
    buffer = (int *) (malloc(sizeof(int)*NUM_SAMPLES_PER_CHANNEL));
}

o1buffer::~o1buffer(){
    free(buffer);
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

    for(int i=0; i< numElements; i++){
        add(firstElement[i], currentAddress);
        currentAddress = (currentAddress + 1) % NUM_SAMPLES_PER_CHANNEL;
    }
    return 0;
}

int o1buffer::addVector(char *firstElement, int numElements){
    int currentAddress = mostRecentAddress;

    for(int i=0; i< numElements; i++){
        add(firstElement[i], currentAddress);
        currentAddress = (currentAddress + 1) % NUM_SAMPLES_PER_CHANNEL;
    }
    return 0;
}

int o1buffer::addVector(unsigned char *firstElement, int numElements){
    int currentAddress = mostRecentAddress;

    for(int i=0; i< numElements; i++){
        add(firstElement[i], currentAddress);
        currentAddress = (currentAddress + 1) % NUM_SAMPLES_PER_CHANNEL;
    }
    return 0;
}

int o1buffer::addVector(short *firstElement, int numElements){
    int currentAddress = mostRecentAddress;

    for(int i=0; i< numElements; i++){
        add(firstElement[i], currentAddress);
        currentAddress = (currentAddress + 1) % NUM_SAMPLES_PER_CHANNEL;
    }
    return 0;
}


int o1buffer::get(int address){
    //Ensure that the address is not too high.
    if(address >= NUM_SAMPLES_PER_CHANNEL){
        address = address % NUM_SAMPLES_PER_CHANNEL;
    }
    if(address<0){
        LIBRADOR_LOG(LOG_ERROR, "ERROR: o1buffer::get was given a negative address\n");
    }
    //Return the value
    return buffer[address];
}

inline void o1buffer::updateMostRecentAddress(int newAddress){
    mostRecentAddress = newAddress;
}

//This function places samples in a buffer than can be plotted on the streamingDisplay.
//A small delay, is added in case the packets arrive out of order.
std::vector<double> *o1buffer::getMany_double(int numToGet, int interval_samples, int delay_samples, int filter_mode, double scope_gain, bool AC, bool twelve_bit_multimeter){
    //Resize the vector
    convertedStream_double.resize(numToGet);

    //Copy raw samples out.
    int tempAddress;
    for(int i=0;i<numToGet;i++){
        tempAddress = mostRecentAddress - delay_samples - (interval_samples * i);
        if(tempAddress < 0){
            tempAddress += NUM_SAMPLES_PER_CHANNEL;
        }
        double *data = convertedStream_double.data();
        data[i] = get_filtered_sample(tempAddress, filter_mode, interval_samples, scope_gain, AC, twelve_bit_multimeter);
        //convertedStream_double.replace(i, buffer[tempAddress]);
    }
    return &convertedStream_double;
}

//Reads each int as 8 bools.  Upper 3 bytes are ignored.
std::vector<uint8_t> *o1buffer::getMany_singleBit(int numToGet, int interval_subsamples, int delay_subsamples){
    //Resize the vector
    convertedStream_digital.resize(numToGet);

    //Copy raw samples out.
    int tempAddress;
    int subsample_current_delay;
    uint8_t mask;
    uint8_t *data = convertedStream_digital.data();
    int tempInt;

    for(int i=0;i<numToGet;i++){
        subsample_current_delay = delay_subsamples + (interval_subsamples * i);
        tempAddress = mostRecentAddress - subsample_current_delay / 8;
        mask = 0x01 << (subsample_current_delay % 8);
        if(tempAddress < 0){
            tempAddress += NUM_SAMPLES_PER_CHANNEL;
        }
        tempInt = get(tempAddress);
        data[i] = (((uint8_t)tempInt) & mask) ? 1 : 0;
    }
    return &convertedStream_digital;
}

std::vector<uint8_t> *o1buffer::getSinceLast_singleBit(int feasible_window_begin, int feasible_window_end, int interval_subsamples) {
    //Calculate what sample the feasible window begins at
    //printf_debugging("o1buffer::getSinceLast()\n")
    int feasible_start_point = mostRecentAddress * 8 - feasible_window_begin;
    if(feasible_start_point < 0){
        feasible_start_point += NUM_SAMPLES_PER_CHANNEL * 8;
    }

    //Work out whether or not we're starting from the feasible window or the last point
    int actual_start_point;
    if(distanceFromMostRecentAddress(feasible_start_point / 8) > distanceFromMostRecentAddress(stream_index_at_last_call + interval_subsamples / 8)){
        actual_start_point = stream_index_at_last_call * 8 + interval_subsamples;
    } else {
        actual_start_point = feasible_start_point;
    }

    //Work out how much we're copying
    int actual_sample_distance = distanceFromMostRecentAddress(actual_start_point / 8) - distanceFromMostRecentAddress(mostRecentAddress - feasible_window_end / 8);
    int numToGet = actual_sample_distance / interval_subsamples;
    //printf_debugging("Fetching %d samples, starting at index %d with interval %d\n", numToGet, actual_start_point, interval_samples);

    //Resize the vector
    convertedStream_digital.resize(numToGet);

    //Copy raw samples out.
    int tempAddress;
    int subsample_current_delay;
    uint8_t mask;
    uint8_t *data = convertedStream_digital.data();
    int tempInt;

    for(int i=0;i<numToGet;i++){
        subsample_current_delay = actual_start_point + (interval_subsamples * i);
        tempAddress = mostRecentAddress - subsample_current_delay / 8;
        mask = 0x01 << (subsample_current_delay % 8);
        if(tempAddress < 0){
            tempAddress += NUM_SAMPLES_PER_CHANNEL;
        }
        tempInt = get(tempAddress);
        data[i] = (((uint8_t)tempInt) & mask) ? 1 : 0;
    }

    //update stream_index_at_last_call for next call
    stream_index_at_last_call = tempAddress;

    return &convertedStream_digital;
}

std::vector<double> *o1buffer::getSinceLast(int feasible_window_begin, int feasible_window_end, int interval_samples, int filter_mode, double scope_gain, bool AC, bool twelve_bit_multimeter){

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
        data[numToGet-1-i] = get_filtered_sample(tempAddress, filter_mode, interval_samples, scope_gain, AC, twelve_bit_multimeter);
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
double o1buffer::get_filtered_sample(int index, int filter_type, int filter_size, double scope_gain, bool AC, bool twelve_bit_multimeter){
    double accum = 0;
    int currentPos = index - (filter_size / 2);
    int end = currentPos + filter_size;

    switch(filter_type){
        case 0: //No filter
            return sampleConvert(buffer[index], scope_gain, AC, twelve_bit_multimeter);
        case 1: //Moving Average filter
            if(currentPos < 0){
                currentPos += NUM_SAMPLES_PER_CHANNEL;
            }
            if(end >= NUM_SAMPLES_PER_CHANNEL){
                end -= NUM_SAMPLES_PER_CHANNEL;
            }
            while(currentPos != end){
                accum += buffer[currentPos];
                currentPos = (currentPos + 1) % NUM_SAMPLES_PER_CHANNEL;
            }
            return sampleConvert(accum/((double)filter_size), scope_gain, AC, twelve_bit_multimeter);
        break;
        default: //Default to "no filter"
            return buffer[index];
    }
}

double o1buffer::sampleConvert(int sample, double scope_gain, bool AC, bool twelve_bit_multimeter){
    double voltageLevel;
    double TOP;

    if(twelve_bit_multimeter){
        TOP = 2048;
    } else TOP = 128;

    voltageLevel = ((double)sample * (vcc/2)) / (frontendGain * scope_gain * TOP);
    if (!twelve_bit_multimeter) voltageLevel += voltage_ref;
    #ifdef MULTIMETER_INVERT
        if(twelve_bit_multimeter) voltageLevel *= -1;
    #endif

    if(AC){
        voltageLevel -= voltage_ref;
    }

    if(twelve_bit_multimeter){
        #pragma message("Hack here.Do not know why this line works, but it does.")
        voltageLevel = voltageLevel / 16;
    }

    return voltageLevel;
}

