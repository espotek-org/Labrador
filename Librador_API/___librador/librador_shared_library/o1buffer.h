#ifndef O1BUFFER_H
#define O1BUFFER_H

#include <vector>
#include <stdint.h>

#define NUM_SAMPLES_PER_CHANNEL (375000 * 60) //1 minute of samples at 375ksps!
#define MULTIMETER_INVERT

class o1buffer
{
public:
    explicit o1buffer();
    ~o1buffer();
    int reset(bool hard);
    void add(int value, int address);
    int addVector(int *firstElement, int numElements);
    int addVector(char *firstElement, int numElements);
    int addVector(unsigned char *firstElement, int numElements);
    int addVector(short *firstElement, int numElements);
    int get(int address);
    int mostRecentAddress = 0;
    int stream_index_at_last_call = 0;
    int distanceFromMostRecentAddress(int index);
    std::vector<double> *getMany_double(int numToGet, int interval_samples, int delay_sample, int filter_mode, double scope_gain, bool AC, bool twelve_bit_multimeter);
    std::vector<uint8_t> *getMany_singleBit(int numToGet, int interval_subsamples, int delay_subsamples);
    std::vector<double> *getSinceLast(int feasible_window_begin, int feasible_window_end, int interval_samples, int filter_mode, double scope_gain, bool AC, bool twelve_bit_multimeter);
    std::vector<uint8_t> *getSinceLast_singleBit(int feasible_window_begin, int feasible_window_end, int interval_subsamples);
    double vcc = 3.3;
    double frontendGain = (75.0/1075.0);
    double voltage_ref = 1.65;
private:
    int *buffer;
    std::vector<double> convertedStream_double;
    std::vector<uint8_t> convertedStream_digital;
    void updateMostRecentAddress(int newAddress);
    double get_filtered_sample(int index, int filter_type, int filter_size, double scope_gain, bool AC, bool twelve_bit_multimeter);
    double sampleConvert(int sample, double scope_gain, bool AC, bool twelve_bit_multimeter);
};

#endif // O1BUFFER_H
