#ifndef O1BUFFER_H
#define O1BUFFER_H

#include <vector>
#include <stdint.h>
#include <mutex>
#include <chrono>

#define NUM_SAMPLES_PER_CHANNEL (375000 * 10) //10 seconds of samples at 375ksps!
#define MULTIMETER_INVERT

class uartStyleDecoder;
struct UartSettings;

class o1buffer
{
public:
    enum TriggerType {Disabled, Rising, Falling};
    struct trigger_settings {
        TriggerType trigger_type = TriggerType::Disabled;
        float trigger_level = 0.f;
        bool is_single_shot = false;
        bool operator==(const trigger_settings& other) const{
            return (trigger_type == other.trigger_type) && (trigger_level == other.trigger_level) && (is_single_shot == other.is_single_shot);
        }
    };
    struct virtual_transform_settings {
        float offset = 0.f;
        int gain = 1;
        bool is_ac = false;
        bool is_paused = false;
        bool operator==(const virtual_transform_settings& other) const{
            return (offset == other.offset) && (gain == other.gain) && (is_ac == other.is_ac) && (is_paused == other.is_paused);
        }
    };
    o1buffer(double sps);
    ~o1buffer();
    int reset(bool hard);
    void add(int value, int address);
    int addVector(int *firstElement, int numElements);
    int addVector(char *firstElement, int numElements);
    int addVector(unsigned char *firstElement, int numElements);
    int addVector(short *firstElement, int numElements);
    int get(int address, bool daq = false);
    int mostRecentAddress = 0;
    int mostRecentAddressPaused = 0;
    int mostRecentAddressDAQ = 0;
    int stream_index_at_last_call = 0;
    int distanceFromMostRecentAddress(int index);
    void resetTrigger(double scope_gain, bool twelve_bit_multimeter);
    std::vector<double> *getMany_double(int numToGet, double interval_samples, int delay_sample, int filter_mode, double scope_gain, bool twelve_bit_multimeter, bool daq = false);
    std::vector<double> *getMany_singleBit(int numToGet, double interval_subsamples, int delay_subsamples, bool daq = false);
    std::vector<double> *getSinceLast(int feasible_window_begin, int feasible_window_end, int interval_samples, int filter_mode, double scope_gain, bool twelve_bit_multimeter);
    double vcc = 3.3;
    // Conversion constants, adjustable via usbCallHandler::set_channel_calibration
    // (Qt parity: isobuffer m_frontendGain / m_voltage_ref).
    // frontendGain: nominal R4/(R3+R4) input-divider gain, scaled by the
    // calibrated gain correction.  voltage_ref: value added to converted scope
    // samples (Qt stores the measured bias v and applies 3.3 - v here).
    double frontendGain = (75.0/1075.0);
    double voltage_ref = 1.65;
    int setPaused(bool is_paused, int mostRecentAddressDelta = 0, bool hard = false);
    void copy_to_daq();
    bool getPaused();
    bool setTriggerSettings(trigger_settings new_trigger_settings);
    bool setVirtualTransformSettings(virtual_transform_settings new_virtual_transform_settings);
    void setUartDecodeSettings(UartSettings new_settings);
    bool isTriggeringEnabled();
    int getDelayIncludingFromTrigger(int delay_samples, int window_samples, bool daq = false, bool* single_shot_reached = NULL, int* trigger_delay_out = NULL);
    double m_samples_per_second;
    int m_bufferLen = NUM_SAMPLES_PER_CHANNEL;
    void UartDecode();
    char * getUart_String(bool* parity_check);
    double get_filtered_sample(int index, int filter_type, int filter_size, double scope_gain, bool twelve_bit_multimeter, bool daq = false);
private:
    trigger_settings m_trigger_settings;
    virtual_transform_settings m_virtual_transform_settings;
    int *buffer;
    int *buffer_paused;
    int *buffer_daq;
    bool *m_is_triggered;
    std::vector<double> convertedStream_double;
    std::vector<double> convertedStream_double_daq;
    std::vector<uint8_t> convertedStream_digital;
    void updateMostRecentAddress(int newAddress);
    double sampleConvert(int sample, double scope_gain, bool twelve_bit_multimeter) const;
    short inverseSampleConvert(double voltageLevel, double scope_gain, bool twelve_bit_multimeter) const;
    enum TriggerSeekState {Invalid, AboveTriggerLevel, BelowTriggerLevel};
    TriggerSeekState m_triggerSeekState = TriggerSeekState::Invalid;
    short m_triggerLevelADC;
    short m_triggerSensitivity;
    const double kTriggerSensitivityMultiplier = 4;
    void checkTriggered(int mostRecentAddress);
    int m_ac_offset_adc_units = 0;
    void computeTriggerLevel();
    uartStyleDecoder* m_uart_decoder = NULL;
};

#endif // O1BUFFER_H
