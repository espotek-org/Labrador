#ifndef USBCALLHANDLER_H
#define USBCALLHANDLER_H

#include "libusb.h"
extern "C"
{
    #include "libdfuprog.h"
}
#include "o1buffer.h"
#include "uartstyledecoder.h"
#include "i2cdecoder.h"
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>

#ifdef PLATFORM_ANDROID
#include <jni.h>
#endif

#define NUM_ISO_ENDPOINTS (1)
#define NUM_FUTURE_CTX (4)
#define ISO_PACKET_SIZE (750)
#define ISO_PACKETS_PER_CTX (33)
#define MAX_SUPPORTED_DEVICE_MODE (7)
#define FGEN_LIMIT (3.2)
#define FGEN_MAX_SAMPLES (512)
#define FGEN_SAMPLE_MIN (5.0)
#define XMEGA_MAIN_FREQ (48000000)
#define PSU_ADC_TOP (128)

// Single source of truth for the firmware the app expects on the board.
// A "v3" variant carrying both interfaces is planned; flip these together
// with the .hex shipped in the app assets.
#define EXPECTED_FIRMWARE_VERSION 0x0007
#define DEFINED_EXPECTED_VARIANT 2
#define LABRADOR_BOOTLOADER_PID 0x2fe4
// "Gobindar" state (Qt Desktop_Interface GOBINDAR_PID): a misconfigured/
// misflashed board that enumerates with PID 0xa000 instead of 0xba94.
// It accepts no useful control traffic; recovery needs the user to force
// the hardware bootloader (short DO1 to GND, replug) and then a reflash.
#define LABRADOR_GOBINDAR_PID 0xa000

//EVERYTHING MUST BE SENT ONE BYTE AT A TIME, HIGH AND LOW BYTES SEPARATE, IN ORDER TO AVOID ISSUES WITH ENDIANNESS.
typedef struct uds{
    volatile char header[9];
    volatile uint8_t trfcntL0;
    volatile uint8_t trfcntH0;
    volatile uint8_t trfcntL1;
    volatile uint8_t trfcntH1;
    volatile uint8_t medianTrfcntL;
    volatile uint8_t medianTrfcntH;
    volatile uint8_t calValNeg;
    volatile uint8_t calValPos;
    volatile uint8_t CALA;
    volatile uint8_t CALB;
    volatile uint8_t outOfRangeL;
    volatile uint8_t outOfRangeH;
    volatile uint8_t counterL;
    volatile uint8_t counterH;
    volatile uint8_t dma_ch0_cntL;
    volatile uint8_t dma_ch0_cntH;
    volatile uint8_t dma_ch1_cntL;
    volatile uint8_t dma_ch1_cntH;

} unified_debug;

typedef struct fGenSettings{
    uint8_t samples[FGEN_MAX_SAMPLES];
    int numSamples = 0;
    uint16_t timerPeriod = 0;
    uint8_t clockDividerSetting = 0;
} fGenSettings;

#define send_control_transfer_with_error_checks(A, B, C, D, E, F) \
    int temp_control_transfer_error_value = send_control_transfer(A,B,C,D,E,F); \
    if(temp_control_transfer_error_value < 0){ \
        return temp_control_transfer_error_value - 1000; \
    }

struct SDL_IOStream;

class usbCallHandler
{
public:
    usbCallHandler(unsigned short VID_in, unsigned short PID_in);
    ~usbCallHandler();
    int setup_usb_control(int file_descriptor);
    int setup_usb_iso();
    void alloc_iso_transfers();
    int submit_iso_transfers();
    void delete_iso_thread();
    int send_control_transfer(uint8_t RequestType, uint8_t Request, uint16_t Value, uint16_t Index, uint16_t Length, unsigned char *LDATA);
    int avrDebug(void);
    int send_device_reset();
    double get_samples_per_second();
    std::vector<double> *getMany_double(int channel, int numToGet, double interval_samples, int delay_sample, int filter_mode, bool daq = false);
    std::vector<double> * getMany_singleBit(int channel, int numToGet, double interval_subsamples, int delay_subsamples, bool daq = false);
    std::vector<double> *getMany_sincelast(int channel, int feasible_window_begin, int feasible_window_end, int interval_samples, int filter_mode);
    bool connected = false;
    //Control Commands
    int set_device_mode(int mode);
    int set_gain(double newGain);
    int update_function_gen_settings(int channel, unsigned char* sampleBuffer, int numSamples, double usecs_between_samples, double amplitude_v, double offset_v);
    int send_function_gen_settings(int channel);
    int set_psu_voltage(double voltage);
    // Oscilloscope calibration (Qt Desktop_Interface 3-stage wizard parity).
    // vref = measured channel bias as the Qt app persists it (CalibrateVrefCHx,
    // neutral 1.65); the conversion adds (3.3 - vref) to every scope sample.
    // gain_scale multiplies the nominal R4/(R3+R4) frontend gain (neutral 1.0).
    // Channel 1 covers both the 375 kSps CHA buffer and the 750 kSps buffer
    // (Qt mirrors CH1 calibration onto its 750 buffer the same way);
    // channel 2 covers the 375 kSps CHB buffer.  Works while disconnected.
    int set_channel_calibration(int channel, double vref, double gain_scale);
    // PSU calibration offset in volts (Qt genericusbdriver psu_offset /
    // QSettings CalibratePsu).  Applied on the next set_psu_voltage call.
    int set_psu_calibration_offset(double offset);
    double get_scope_gain();
    int set_digital_state(uint8_t digState);
    int reset_device(bool goToBootloader);
    uint16_t get_firmware_version();
    uint8_t get_firmware_variant();
    int set_synchronous_pause_state(bool newState);
    int setPaused(int channel, bool is_paused);
    bool getPaused(int channel);
    void setTriggerSettings(int channel, o1buffer::trigger_settings new_trigger_settings);
    void setVirtualTransformSettings(int channel, o1buffer::virtual_transform_settings new_virtual_transform_settings);
    template <typename T>
    void setSettingsForChannel(int ch, T channel_scope_settings, o1buffer* ch1_375, o1buffer* ch2_375, o1buffer* ch1_750, bool (o1buffer::*setSettings)(T));
    void setUartDecodeSettings(int channel, UartSettings new_settings);
    char * getUart_String(int channel, bool* parity_check);
    char * getI2c_String();
    void setI2cIsDecoding(bool new_decode_on);
    bool isoThreadIsActive();
    void teardown_connection();        // stop iso thread, free transfers, close handle
    int init_libusb();
    void respondToStartupOrUsbStateChange(bool is_plugged_in, int file_descriptor, bool bootloader_mode);
    void set_bootloader_mode_allowed(bool allowed);
    void initiateFirmwareFlash();
#ifndef PLATFORM_ANDROID
    // Desktop connection path: the app polls for the device and connects
    // directly (no fd injection, libusb enumerates devices itself).
    bool desktop_device_present(bool* bootloader_mode_out);
    // Extended presence scan: reports the application-firmware (0xba94),
    // bootloader (0x2fe4) and Gobindar (0xa000) devices independently.
    // Returns true if any of the three is attached.
    bool desktop_device_present_ex(bool* app_mode_out, bool* bootloader_mode_out, bool* gobindar_mode_out);
    int desktop_connect();
    void desktop_disconnect();
    int desktop_flash_firmware(const char* hex_path);  // blocking, seconds
    // Board found stuck in bootloader mode: try a plain launch first (firmware
    // may be intact), full reflash only if it doesn't come back healthy.
    int desktop_bootloader_recover(const char* hex_path);  // blocking, seconds
    // Board found in Gobindar state (PID 0xa000, Qt deGobindarise parity):
    // the caller must FIRST tell the user to short Digital Out 1 to GND and
    // replug the board (forcing the hardware bootloader), then call this.
    // Blocks waiting for the bootloader to enumerate (user-paced, minutes),
    // then runs the full erase + flash + launch cycle.
    int desktop_gobindar_recover(const char* hex_path);  // blocking, minutes
    // USB port reset (software replug): clears the wedged post-flash state
    // where the ADC pipeline never starts and the board streams a constant.
    int desktop_hard_reset();
#endif

    // DAQ
    enum daqUnitOptions {Volts, ADC, Bits, None, QUANT};
    const static char* daq_unit_labels[] ;//= {"Volts", "ADC", "Bits", "None"};// TODO: allow DAQ of decoded chars
    static constexpr bool daqUnitIsForScope[daqUnitOptions::QUANT] = {true, true, false, false};
    void spawn_daq_thread(int channel, int numToGet, int interval_samples, daqUnitOptions units_sel[2], const char* filename);
    void drive_daq(int channel, int numToGet, int interval_samples, daqUnitOptions units_sel[2], const char * filename);
    void daq_for_channel(int channel, int numToGet, int interval_samples, daqUnitOptions units_sel, SDL_IOStream* iostream);
    bool poll_daq_status();

private:

    unsigned short VID, PID;
    libusb_context *ctx = nullptr;
    libusb_device_handle *handle = nullptr;
    unsigned char inBuffer[256];

    //USBIso Vars
    unsigned char pipeID[NUM_ISO_ENDPOINTS];
    libusb_transfer *isoCtx[NUM_ISO_ENDPOINTS][NUM_FUTURE_CTX];
    unsigned char dataBuffer[NUM_ISO_ENDPOINTS][NUM_FUTURE_CTX][ISO_PACKET_SIZE*ISO_PACKETS_PER_CTX];
    std::thread *iso_polling_thread = nullptr;
    std::thread *daq_thread = nullptr;
    //Control Vars
    uint8_t fGenTriple = 0;
    fGenSettings functionGen_CH1;
    fGenSettings functionGen_CH2;
    double gain_psu = 1;
    double vref_psu = 1.65;
    // PSU calibration offset (volts); see set_psu_calibration_offset.
    double psu_offset = 0;
    uint16_t gainMask = 0x0000;
    double current_scope_gain = 1;
    bool synchronous_pause_state = false;
    i2c::i2cDecoder* m_i2c_decoder;

    bool starting_after_flash = false;

    int flashFirmware(int file_descriptor);
    void closeDevice_cpp();
    int findDevice_cpp();
    void dfu_launch();
    int claim_and_prepare();           // detach kernel driver + claim interface 0
    int check_firmware_and_start_iso();// version/variant check, then iso stream or flash request

    friend void isoCallback(struct libusb_transfer * transfer);
    int deviceMode = 0;

    o1buffer *internal_o1_buffer_375_CHA;
    o1buffer *internal_o1_buffer_375_CHB;
    o1buffer *internal_o1_buffer_750;

    int begin_iso_thread_shutdown();
    bool is_iso_thread_shutdown_requested();
    int decrement_remaining_transfers();
    void iso_polling_function(libusb_context *ctx);
    bool safe_to_exit_thread();


    bool iso_thread_shutdown_requested = false;
    int iso_thread_shutdown_remaining_transfers = NUM_FUTURE_CTX;
    std::atomic<bool> iso_thread_active = false;
    std::atomic<bool> daq_thread_active = false ;

    std::mutex iso_thread_shutdown_mutex;
    std::mutex buffer_read_write_mutex;
    // Serialises teardown_connection: during a firmware flash the worker
    // thread and the main thread's disconnect detection can both reach it.
    std::mutex teardown_mutex;

    static SDL_IOStream* open_file(const char * filepath);
};

template <typename T>
void usbCallHandler::setSettingsForChannel(int ch, T channel_scope_settings, o1buffer* ch1_375, o1buffer* ch2_375, o1buffer* ch1_750, bool (o1buffer::*setSettings)(T))
{
    bool trigger_reset_needed;
    if(ch==1) {
        trigger_reset_needed = (ch1_375->*setSettings)(channel_scope_settings);
        if(trigger_reset_needed)
            ch1_375->resetTrigger(current_scope_gain, deviceMode==7);
        trigger_reset_needed = (ch1_750->*setSettings)(channel_scope_settings);
        if(trigger_reset_needed)
            ch1_750->resetTrigger(current_scope_gain, deviceMode==7);
    } else if (ch==2) {
        trigger_reset_needed = (ch2_375->*setSettings)(channel_scope_settings);
        if(trigger_reset_needed)
            ch2_375->resetTrigger(current_scope_gain, deviceMode==7);
    }
}
#endif // USBCALLHANDLER_H
