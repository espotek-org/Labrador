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
#include <deque>
#include <chrono>
#include <atomic>
#include <condition_variable>

#ifdef PLATFORM_ANDROID
#include <jni.h>
#endif

#define NUM_FUTURE_CTX (4)
#define ISO_PACKET_SIZE (750)
#define ISO_PACKETS_PER_CTX (33)
#define MAX_SUPPORTED_DEVICE_MODE (7)
#define FGEN_LIMIT (3.2)
#define FGEN_MAX_SAMPLES (512)
#define FGEN_SAMPLE_MIN (5.0)
#define XMEGA_MAIN_FREQ (48000000)
#define PSU_ADC_TOP (128)

// AIO (variant 3) firmware transport layout: three interfaces, each with
// alt setting 0 = no endpoints and alt setting 1 = the streaming endpoints.
//   iface 0 (iso6): iso IN 0x81..0x86 (128B) + 8-byte iso meta EP 0x89
//   iface 1 (iso1): iso IN 0x87 (1023B, carries 750B/frame) + meta EP 0x8a
//   iface 2 (bulk): bulk IN 0x88 (64B packets, 8-byte in-stream headers)
// Every frame is protected by {magic, seq16, checksum}: in-stream ahead of
// each bulk payload (EB 57), lag-1 on the iso meta endpoints (EB 58, which
// carry the checksum of BOTH double-buffer halves - a frame is valid if it
// matches either, stomped by the ADC/DMA loop if it matches neither).
#define LABRADOR_TRANSPORT_AUTO 0
#define LABRADOR_TRANSPORT_ISO6 1
#define LABRADOR_TRANSPORT_ISO1 2
#define LABRADOR_TRANSPORT_BULK 3

#define AIO_IFACE_ISO6 0
#define AIO_IFACE_ISO1 1
#define AIO_IFACE_BULK 2
#define AIO_EP_ISO6_FIRST 0x81
#define AIO_EP_ISO6_COUNT 6
#define AIO_EP_ISO1 0x87
#define AIO_EP_BULK 0x88
#define AIO_EP_ISO6_META 0x89
#define AIO_EP_ISO1_META 0x8a
#define AIO_ISO6_PACKET_SIZE 128
#define AIO_META_PACKET_SIZE 8
#define AIO_HDR_MAGIC0 0xEB
#define AIO_HDR_MAGIC1_BULK 0x57
#define AIO_HDR_MAGIC1_META 0x58
// Bulk framing: every transfer is padded to a 64-byte multiple so the
// stream never contains a short packet (a short packet would terminate a
// queued URB early and collapse the read-ahead runway).  Each frame is a
// 64-byte header block (8 meaningful bytes, zero pad) followed by a
// 768-byte payload block (750 data + 18 pad) - a fixed 832-byte stride.
#define AIO_BULK_HDR_XFER 64
#define AIO_BULK_PAYLOAD_XFER 768
#define MAX_DATA_ENDPOINTS 6
// Bulk URB queue depth: the host controller keeps draining the pipe into
// pre-queued kernel buffers even while the app's event thread is busy, so
// this is the stall runway protecting the device's 1 ms drain deadline
// (ADC overwrites its double buffer every 2 ms).  16 x 16 KB = 256 KB =
// ~340 ms at full rate.
#define NUM_BULK_CTX 16
#define BULK_CTX_SIZE 16384

// Single source of truth for the firmware the app expects on the board.
// Flip these together with the .hex shipped in the app assets.
#define EXPECTED_FIRMWARE_VERSION 0x000C
#define DEFINED_EXPECTED_VARIANT 3
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
    // On-device calibration storage (EEPROM page via vendor requests
    // 0xac/0xad; firmware >= 0x000A).  Layout: magic CA 1B, version 1,
    // five LE floats {vref_ch1, gain_scale_ch1, vref_ch2, gain_scale_ch2,
    // psu_offset}, xor checksum.  NOTE: a DFU chip erase wipes EEPROM, so
    // the host should re-save after flashing firmware.
    // load returns 0 on success, 1 if the device has no valid calibration
    // stored, <0 on transfer errors.
    int save_calibration_to_device(double vref_ch1, double gain_scale_ch1,
        double vref_ch2, double gain_scale_ch2, double psu_offset);
    int load_calibration_from_device(double *vref_ch1, double *gain_scale_ch1,
        double *vref_ch2, double *gain_scale_ch2, double *psu_offset);
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
    // Transport selection.  Must be set before connecting; AUTO resolves to
    // the platform default (macOS: bulk; win64: iso6; win32/linux/android:
    // iso1).  On macOS the iso transports are refused unless the build
    // defines LIBRADOR_MACOS_ALLOW_ISO - opening a full-speed iso pipe
    // panics the kernel on macOS Tahoe (IOUSBHostFamily getEndpointMult
    // NULL-dereferences the SuperSpeed companion descriptor).
    int set_transport(int transport);
    int get_active_transport();
    // Frame-validation statistics accumulated from the per-frame headers.
    void get_frame_stats(uint64_t *ok, uint64_t *bad_checksum, uint64_t *dropped, uint64_t *unvalidated);
    void reset_frame_stats();
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
    // Data endpoints for the active transport (1 for iso1, 6 for iso6),
    // plus one meta endpoint for the iso transports.  Buffers are sized for
    // the worst case (750-byte packets).
    int num_data_endpoints = 1;
    unsigned char pipeID[MAX_DATA_ENDPOINTS];
    unsigned char meta_pipeID = 0;
    int data_packet_size = ISO_PACKET_SIZE;
    libusb_transfer *isoCtx[MAX_DATA_ENDPOINTS][NUM_FUTURE_CTX];
    libusb_transfer *metaCtx[NUM_FUTURE_CTX];
    unsigned char dataBuffer[MAX_DATA_ENDPOINTS][NUM_FUTURE_CTX][ISO_PACKET_SIZE*ISO_PACKETS_PER_CTX];
    unsigned char metaBuffer[NUM_FUTURE_CTX][AIO_META_PACKET_SIZE*ISO_PACKETS_PER_CTX];
    // Bulk transport: queued bulk reads parsed as a framed byte stream.
    libusb_transfer *bulkCtx[NUM_BULK_CTX];
    unsigned char bulkBuffer[NUM_BULK_CTX][BULK_CTX_SIZE];
    std::vector<unsigned char> bulk_stream;
    // Transport state
    int requested_transport = LABRADOR_TRANSPORT_AUTO;
    int active_transport = LABRADOR_TRANSPORT_AUTO;   // resolved at connect
    int claimed_iface = -1;
    bool iface0_claimed = false;
    int alloc_transport = -1;   // transport the transfer arrays were built for
    int total_inflight_transfers();
    // Frame validation state (event-loop thread only, except the atomics)
    std::atomic<uint64_t> frames_ok{0};
    std::atomic<uint64_t> frames_bad_checksum{0};
    std::atomic<uint64_t> frames_dropped{0};
    std::atomic<uint64_t> frames_unvalidated{0};
    bool seq_started = false;
    uint16_t last_seq = 0;
    // Recent reassembled-frame checksums for meta (lag-1) pairing:
    // ring indexed by frame counter, holds the XOR of each 750-byte frame.
    // Must exceed the largest frame backlog between a data URB completion
    // and the matching meta URB completion: iso URBs batch 33 frames, and
    // data/meta URBs can complete a full batch apart, so 16 was too small
    // (every meta missed the ring and counted as unvalidated).
    static const int CSUM_RING_SIZE = 128;
    unsigned char frame_csum_ring[CSUM_RING_SIZE];
    bool frame_csum_valid[CSUM_RING_SIZE];
    uint64_t data_frame_counter = 0;
    uint64_t meta_frame_counter = 0;
    // iso6 reassembly: per-endpoint packet queues consumed in lockstep
    std::deque<std::vector<unsigned char>> iso6_queues[MAX_DATA_ENDPOINTS];

    // Hand-off between the libusb event thread and the ingest thread.  The
    // event thread must NEVER block on buffer_read_write_mutex: while it
    // is blocked it cannot resubmit bulk URBs, the host controller stops
    // issuing IN tokens, and the device blows the 1 ms drain deadline (the
    // ADC overwrites its double buffer every 2 ms).  Callbacks push raw
    // bytes here under a short-lived lock; the ingest thread does all
    // parsing, validation and o1buffer dispatch.
    struct IngestItem {
        uint8_t kind;        // 0=bulk bytes, 1=iso data, 2=iso6 data, 3=meta
        uint8_t ep_index;    // iso6 endpoint index
        std::vector<unsigned char> bytes;
    };
    std::deque<IngestItem> ingest_queue;
    std::mutex ingest_mutex;
    std::condition_variable ingest_cv;
    std::thread *ingest_thread = nullptr;
    bool ingest_stop = false;
    void ingest_thread_function();
    void queue_ingest(uint8_t kind, uint8_t ep_index, const unsigned char *data, int length);

    // Timebase preservation: lost or torn frames are replaced with a
    // repeat of the last good frame (sample-and-hold) so the sample clock
    // in the o1buffers stays honest instead of silently compressing.
    unsigned char last_good_frame[ISO_PACKET_SIZE] = {0};
    void dispatch_lost_frames(uint64_t count);
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

    // Firmware identity cache: valid from the first successful reads after
    // connect until teardown_connection.
    uint16_t cached_firmver = 0;
    uint8_t cached_variant = 0;
    bool fw_version_cached = false;
    bool fw_variant_cached = false;

    int flashFirmware(int file_descriptor);
    void closeDevice_cpp();
    int findDevice_cpp();
    void dfu_launch();
    int claim_and_prepare();           // detach kernel driver + claim interface 0
    int check_firmware_and_start_iso();// version/variant check, then iso stream or flash request

    friend void isoCallback(struct libusb_transfer * transfer);
    friend void metaIsoCallback(struct libusb_transfer * transfer);
    friend void bulkCallback(struct libusb_transfer * transfer);
    int deviceMode = 0;

    // Transport plumbing (all called on the event-loop thread unless noted)
    int resolve_transport();                     // AUTO -> platform default, macOS iso lockout
    int configure_transport_endpoints();         // fill pipeID/meta/packet sizes
    int start_streaming();                       // claim iface, alt 1, submit transfers
    void dispatch_frame(unsigned char *frame750);// deviceMode switch into o1buffers
    void note_frame_csum(unsigned char csum, bool valid);
    void handle_data_iso_packet(unsigned char *data, int length);
    void handle_iso6_packet(int ep_index, unsigned char *data, int length);
    void drain_iso6_queues();
    void handle_meta_packet(unsigned char *data, int length);
    void handle_bulk_bytes(unsigned char *data, int length);
    void note_seq(uint16_t seq);
    void reset_frame_state();

    o1buffer *internal_o1_buffer_375_CHA;
    o1buffer *internal_o1_buffer_375_CHB;
    o1buffer *internal_o1_buffer_750;

    int begin_iso_thread_shutdown();
    bool is_iso_thread_shutdown_requested();
    int decrement_remaining_transfers();
    void free_transfers();
    void rearm_or_retire(struct libusb_transfer *transfer);
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
