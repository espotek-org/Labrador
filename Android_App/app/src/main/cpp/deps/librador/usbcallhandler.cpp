#include "usbcallhandler.h"
//#include <stdio.h>

#include <dlfcn.h>
#include "logging_internal.h"
#include "uartstyledecoder.h"
#include <mutex>
#include <thread>

extern "C"
{
    #include "SDL_iostream_c.h"
}
#ifdef PLATFORM_ANDROID
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include "SDL_android.h"
#endif

void LIBUSB_CALL isoCallback(struct libusb_transfer * transfer){
    //Thread mutex??
    //printf("Copy the data...\n");
    usbCallHandler *usb_driver = (usbCallHandler *)transfer->user_data;
    if(transfer->status==LIBUSB_TRANSFER_COMPLETED)
    {
        usb_driver->buffer_read_write_mutex.lock(); 
        for(int i=0;i<transfer->num_iso_packets;i++){
            unsigned char *packetPointer = libusb_get_iso_packet_buffer_simple(transfer, i);
            //TODO: a switch statement here to handle all the modes.
            switch(usb_driver->deviceMode){
            case 0:
                usb_driver->internal_o1_buffer_375_CHA->addVector((char*) packetPointer, 375);
                break;
            case 1:
                usb_driver->internal_o1_buffer_375_CHA->addVector((char*) packetPointer, 375);
                usb_driver->internal_o1_buffer_375_CHB->addVector((unsigned char*) &packetPointer[375], 375);
                break;
            case 2:
                usb_driver->internal_o1_buffer_375_CHA->addVector((char*) packetPointer, 375);
                usb_driver->internal_o1_buffer_375_CHB->addVector((char*) &packetPointer[375], 375);
                break;
            case 3:
                usb_driver->internal_o1_buffer_375_CHA->addVector((unsigned char*) packetPointer, 375);
                break;
            case 4:
                usb_driver->internal_o1_buffer_375_CHA->addVector((unsigned char*) packetPointer, 375);
                usb_driver->internal_o1_buffer_375_CHB->addVector((unsigned char*) &packetPointer[375], 375);
                break;
            case 6:
                usb_driver->internal_o1_buffer_750->addVector((char*) packetPointer, 750);
                break;
            case 7:
                usb_driver->internal_o1_buffer_375_CHA->addVector((short*) packetPointer, 375);
                break;
            }
        }
        usb_driver->buffer_read_write_mutex.unlock();
    }

    //printf("Re-arm the endpoint...\n");
    if(!usb_driver->is_iso_thread_shutdown_requested()){
        int error = libusb_submit_transfer(transfer);
        if(error){
            LIBRADOR_LOG(LOG_WARNING, "Error re-arming the endpoint!\n");
            usb_driver->begin_iso_thread_shutdown();
            usb_driver->decrement_remaining_transfers();
            LIBRADOR_LOG(LOG_WARNING, "Transfer not being rearmed!  %d armed transfers remaining\n", usb_driver->iso_thread_shutdown_remaining_transfers);
        }
    } else {
        usb_driver->decrement_remaining_transfers();
        LIBRADOR_LOG(LOG_WARNING, "Transfer not being rearmed!  %d armed transfers remaining\n", usb_driver->iso_thread_shutdown_remaining_transfers);
    }
    return;
}

const char* usbCallHandler::daq_unit_labels[] = {"Volts", "ADC", "Bits", "None"};// TODO: allow DAQ of decoded chars

int usbCallHandler::begin_iso_thread_shutdown(){
    iso_thread_shutdown_mutex.lock();
    iso_thread_shutdown_requested = true;
    iso_thread_shutdown_mutex.unlock();
    return 0;
}

bool usbCallHandler::is_iso_thread_shutdown_requested(){
    bool tempReturn;
    iso_thread_shutdown_mutex.lock();
    tempReturn = iso_thread_shutdown_requested;
    iso_thread_shutdown_mutex.unlock();
    return tempReturn;
}

int usbCallHandler::decrement_remaining_transfers(){
    iso_thread_shutdown_mutex.lock();
    iso_thread_shutdown_remaining_transfers--;
    iso_thread_shutdown_mutex.unlock();
    return 0;
}

bool usbCallHandler::safe_to_exit_thread(){
    bool tempReturn;
    iso_thread_shutdown_mutex.lock();
    tempReturn = (iso_thread_shutdown_remaining_transfers == 0);
    iso_thread_shutdown_mutex.unlock();
    return tempReturn;
}


// it makes sense to call this iso_polling_function because we only use libusb's asynchronous API for isochronous transfers
void usbCallHandler::iso_polling_function(libusb_context *ctx){
    LIBRADOR_LOG(LOG_DEBUG, "iso_polling_function thread spawned\n");
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;//ISO_PACKETS_PER_CTX*4000;
    while(!safe_to_exit_thread()){
        //printf("iso_polling_function begin loop\n");
        if(libusb_event_handling_ok(ctx)){
            libusb_handle_events_timeout(ctx, &tv);
        }
    }
    iso_thread_active = false;
    LIBRADOR_LOG(LOG_DEBUG, "iso_polling_function thread finished\n");
}

usbCallHandler::usbCallHandler(unsigned short VID_in, unsigned short PID_in)
{
    VID = VID_in;
    PID = PID_in;

    for(int k=0; k<NUM_ISO_ENDPOINTS; k++){
        pipeID[k] = 0x81+k;
        LIBRADOR_LOG(LOG_DEBUG, "pipeID %d = %d\n", k, pipeID[k]);
    }

    internal_o1_buffer_375_CHA = new o1buffer(375e3);
    internal_o1_buffer_375_CHB = new o1buffer(375e3);
    internal_o1_buffer_750 = new o1buffer(750e3);
    m_i2c_decoder = new i2c::i2cDecoder(internal_o1_buffer_375_CHA, internal_o1_buffer_375_CHB);

    //In case it was deleted before; reset the shared variables.
    iso_thread_shutdown_requested = false;
    iso_thread_shutdown_remaining_transfers = NUM_FUTURE_CTX;
}


usbCallHandler::~usbCallHandler(){
    //Kill off iso_polling_thread.  Maybe join then get it to detect its own timeout condition.
    LIBRADOR_LOG(LOG_DEBUG, "Calling destructor for librador USB call handler\n");

    if(iso_polling_thread)
    {
        begin_iso_thread_shutdown();
        LIBRADOR_LOG(LOG_DEBUG, "Shutting down USB polling thread...\n");
        iso_polling_thread->join();
        LIBRADOR_LOG(LOG_DEBUG, "USB polling thread stopped.\n");
        delete iso_polling_thread;

        for (int i=0; i<NUM_FUTURE_CTX; i++){
            for (int k=0; k<NUM_ISO_ENDPOINTS; k++){
                libusb_free_transfer(isoCtx[k][i]);
            }
        }
        LIBRADOR_LOG(LOG_DEBUG, "Transfers freed.\n");
    }

    if(daq_thread && daq_thread->joinable()){
        daq_thread->join();
    }

    if(handle){
        libusb_release_interface(handle, 0);
        LIBRADOR_LOG(LOG_DEBUG, "Interface released\n");
        libusb_close(handle);
        LIBRADOR_LOG(LOG_DEBUG, "Device Closed\n");
    }
    if(ctx){
        libusb_exit(ctx);
        LIBRADOR_LOG(LOG_DEBUG, "Libusb exited\n");
    }

    delete m_i2c_decoder;
    LIBRADOR_LOG(LOG_DEBUG, "librador USB call handler deleted\n");
}

#ifdef PLATFORM_ANDROID
void usbCallHandler::set_bootloader_mode_allowed(bool allowed) {
    JNIEnv *env = (JNIEnv *) SDL_GetAndroidJNIEnv();
    jobject MainActivityObject = (jobject) SDL_GetAndroidActivity();
    jclass MainActivity(env->GetObjectClass(MainActivityObject));
    jfieldID bootloader_mode_allowedID = env->GetFieldID(MainActivity, "bootloader_mode_allowed", "Z");
    env->SetBooleanField(MainActivityObject, bootloader_mode_allowedID, allowed);
    LIBRADOR_LOG(LOG_DEBUG, "bootloader_mode_allowed set");
}

// call sequence: usbcallhandler.cpp(requestFirmwareFlash) -> MainActivity.java -> librador -> here, all just to get a dialog window before reset_device (below) triggers a usb permission request
void usbCallHandler::initiateFirmwareFlash()
{
    set_bootloader_mode_allowed(true);
    reset_device(true); // will trigger respondToStartupOrUsbStateChange with the device in bootloader mode.  
    connected = false;
    libusb_release_interface(handle, 0);
    libusb_close(handle);
    libusb_exit(ctx);
    ctx = nullptr;
    handle=  nullptr;
}
#endif

//Initialise libusb
int usbCallHandler::init_libusb(){
    if(ctx){
        LIBRADOR_LOG(LOG_DEBUG, "There is already a libusb context!\n");
        return 1;
    } else LIBRADOR_LOG(LOG_DEBUG, "libusb context is null\n");
    struct libusb_init_option libusb_options[2] = {
        {.option = LIBUSB_OPTION_NO_DEVICE_DISCOVERY},
        {.option = LIBUSB_OPTION_LOG_LEVEL, .value = {.ival = 3}}
    };
    int error = libusb_init_context(&ctx, libusb_options, 2);
    if(error){
        LIBRADOR_LOG(LOG_WARNING, "libusb_init FAILED\n");
        return -1;
    } else {
        LIBRADOR_LOG(LOG_DEBUG, "Libusb context initialised\n");
        return 0;
    }
}

int usbCallHandler::setup_usb_control(int file_descriptor){
    LIBRADOR_LOG(LOG_DEBUG, "usbCallHandler::setup_usb_control()\n");
    //Get a handle on the Labrador device
    libusb_wrap_sys_device(ctx, file_descriptor, &handle);

    if(!handle){
        LIBRADOR_LOG(LOG_DEBUG, "DEVICE NOT FOUND\n");
        return -2;
    }
    LIBRADOR_LOG(LOG_DEBUG, "Device found!!\n");

    if(libusb_kernel_driver_active(handle, 0)) {
        LIBRADOR_LOG(LOG_DEBUG, "KERNEL DRIVER ACTIVE");
    } else {
        LIBRADOR_LOG(LOG_DEBUG, "KERNEL DRIVER INACTIVE");
    }
    if(libusb_kernel_driver_active(handle, 0)){
        libusb_detach_kernel_driver(handle, 0);
    }

    //Claim the interface
    int error = libusb_claim_interface(handle, 0);
    if(error){
        LIBRADOR_LOG(LOG_WARNING, "libusb_claim_interface FAILED\n");
        libusb_close(handle);
        handle = nullptr;
        return -3;
    } else LIBRADOR_LOG(LOG_DEBUG, "Interface claimed!\n");
    return 0;
}

// should only be called if iso_polling_thread is not active.  This means either the thread has never been set up OR it was previously set up but has exited iso_polling_function.
int usbCallHandler::setup_usb_iso(){
    LIBRADOR_LOG(LOG_DEBUG, "usbCallHandler::setup_usb_iso()\n");
    if(iso_polling_thread) {
        LIBRADOR_LOG(LOG_DEBUG, "iso polling thead already exists");
        return -1;
    } else {
        LIBRADOR_LOG(LOG_DEBUG, "creating iso polling thread");

        alloc_iso_transfers();

        int error = submit_iso_transfers();
        if(error) {
            return error;
            LIBRADOR_LOG(LOG_WARNING, "setup_usb_iso failed\n");
        }
        iso_polling_thread = new std::thread(&usbCallHandler::iso_polling_function, this, ctx);
        iso_thread_active = true;
    }
    return 0;
}

void usbCallHandler::alloc_iso_transfers(){
    for(int n=0;n<NUM_FUTURE_CTX;n++){
        for (unsigned char k=0;k<NUM_ISO_ENDPOINTS;k++){
            isoCtx[k][n] = libusb_alloc_transfer(ISO_PACKETS_PER_CTX);
            libusb_fill_iso_transfer(isoCtx[k][n], handle, pipeID[k], dataBuffer[k][n], ISO_PACKET_SIZE*ISO_PACKETS_PER_CTX, ISO_PACKETS_PER_CTX, isoCallback, this, 150); // assume the NUM_FUTURE_CTX=4 iso transfers are FIFO, in which case timeout should be > 1 ms (amount of data per packet) * NUM_PACKETS_PER_CTX * NUM_FUTURE_CTX 
            libusb_set_iso_packet_lengths(isoCtx[k][n], ISO_PACKET_SIZE);
        }
    }
}

int usbCallHandler::submit_iso_transfers(){
    for(int n=0;n<NUM_FUTURE_CTX;n++){
        for (unsigned char k=0;k<NUM_ISO_ENDPOINTS;k++){
            int error = libusb_submit_transfer(isoCtx[k][n]);
            if(error){
                LIBRADOR_LOG(LOG_ERROR, "libusb_submit_transfer #%d:%d FAILED with error %d %s\n", n, k, error, libusb_error_name(error));
                return error;
            }
        }
    }
    return 0;
}

int usbCallHandler::send_control_transfer(uint8_t RequestType, uint8_t Request, uint16_t Value, uint16_t Index, uint16_t Length, unsigned char *LDATA){
    unsigned char *controlBuffer;

    if(!connected){
        LIBRADOR_LOG(LOG_DEBUG, "Control packet requested before device has connected!\n");
        return -1;
    }

    if (!LDATA){
        controlBuffer = inBuffer;
    }
    else controlBuffer = LDATA;

    int error = libusb_control_transfer(handle, RequestType, Request, Value, Index, controlBuffer, Length, 4000);
    if(error<0){
        LIBRADOR_LOG(LOG_WARNING, "send_control_transfer FAILED with error %s", libusb_error_name(error));
        connected = false;
        return error - 100;
    }
    return 0;
}


int usbCallHandler::avrDebug(void){
    send_control_transfer_with_error_checks(0xc0, 0xa0, 0, 0, sizeof(unified_debug), nullptr);

    LIBRADOR_LOG(LOG_DEBUG, "unified debug is of size %lu\n", sizeof(unified_debug));

    unified_debug *udsPtr = (unified_debug *) inBuffer;
    uint16_t trfcnt0 = (udsPtr->trfcntH0 << 8) + udsPtr->trfcntL0;
    uint16_t trfcnt1 = (udsPtr->trfcntH1 << 8) + udsPtr->trfcntL1;
    uint16_t medianTrfcnt = (udsPtr->medianTrfcntH << 8) + udsPtr->medianTrfcntL;
    uint16_t outOfRange = (udsPtr->outOfRangeH << 8) + udsPtr->outOfRangeL;
    uint16_t counter = (udsPtr->counterH << 8) + udsPtr->counterL;
    uint16_t dma_ch0_cnt = (udsPtr->dma_ch0_cntH << 8) + udsPtr->dma_ch0_cntL;
    uint16_t dma_ch1_cnt = (udsPtr->dma_ch1_cntH << 8) + udsPtr->dma_ch1_cntL;

    LIBRADOR_LOG(LOG_DEBUG, "%s", udsPtr->header);
    LIBRADOR_LOG(LOG_DEBUG, "trfcnt0 = %d\n", trfcnt0);
    LIBRADOR_LOG(LOG_DEBUG, "trfcnt1 = %d\n", trfcnt1);
    LIBRADOR_LOG(LOG_DEBUG, "medianTrfcnt = %d\n", medianTrfcnt);
    LIBRADOR_LOG(LOG_DEBUG, "outOfRange = %d\n", outOfRange);
    LIBRADOR_LOG(LOG_DEBUG, "counter = %d\n", counter);
    LIBRADOR_LOG(LOG_DEBUG, "calValNeg = %d\n", udsPtr->calValNeg);
    LIBRADOR_LOG(LOG_DEBUG, "calValPos = %d\n", udsPtr->calValPos);
    LIBRADOR_LOG(LOG_DEBUG, "CALA = %d\n", udsPtr->CALA);
    LIBRADOR_LOG(LOG_DEBUG, "CALB = %d\n", udsPtr->CALB);
    LIBRADOR_LOG(LOG_DEBUG, "dma_ch0_cnt = %d\n", dma_ch0_cnt);
    LIBRADOR_LOG(LOG_DEBUG, "dma_ch1_cnt = %d\n", dma_ch1_cnt);

    return 0;
}

void usbCallHandler::spawn_daq_thread(int channel, int numToGet, int interval_samples, daqUnitOptions units_sel[2], const char* filename) {
    daq_thread_active = true;
    daq_thread = new std::thread(&usbCallHandler::drive_daq, this, channel, numToGet, interval_samples, units_sel, filename);
    daq_thread_active = false;
}

bool usbCallHandler::poll_daq_status() {
    if(daq_thread_active) {
        return true;
    } else {
        if(daq_thread && daq_thread->joinable()) {
            daq_thread->join();
            daq_thread = nullptr;
        }
        return false;
    }
}

void usbCallHandler::daq_for_channel(int channel, int numToGet, int interval_samples, daqUnitOptions unit_sel, SDL_IOStream* iostream) {
    o1buffer* buffer_for_daq;
    if(channel==1) {
        // TODO: mutex needed for deviceMode
        if(deviceMode==6) {
            buffer_for_daq = internal_o1_buffer_750;
        } else {
            buffer_for_daq = internal_o1_buffer_375_CHA;
        }
    } else {
        buffer_for_daq = internal_o1_buffer_375_CHB;
    }
    buffer_for_daq->copy_to_daq();
    const char* volts_fmt_template = "%%.%dg ";
    char volts_fmt[8];
    if(deviceMode==7) {
        sprintf(volts_fmt, volts_fmt_template, 4);
    } else {
        sprintf(volts_fmt, volts_fmt_template, 3);
    }

    const char* ch_names[2] = {"CH A", "CH B"};
    SDL_IOprintf(iostream, "%s\n", ch_names[channel-1]);
    if(unit_sel==usbCallHandler::daqUnitOptions::Bits) {
        std::vector<double>* daq_vals = getMany_singleBit(channel, numToGet * 8, interval_samples, 0, true);
        for(auto it = (*daq_vals).rbegin(); it != (*daq_vals).rend(); it++)
            SDL_IOprintf(iostream, "%.0f ", *it);
    } else if (unit_sel==usbCallHandler::daqUnitOptions::Volts) {
        // volts
        std::vector<double>* daq_vals = getMany_double(channel, numToGet, interval_samples, 0, 0, true);
        for(auto it = (*daq_vals).rbegin(); it != (*daq_vals).rend(); it++)
            SDL_IOprintf(iostream, volts_fmt, *it);
    } else if (unit_sel==usbCallHandler::daqUnitOptions::ADC){
        // adc units
        int ix;
        for(int i = 0; i < numToGet; i++) {
            int i2 = buffer_for_daq->mostRecentAddressDAQ - i;
            ix = i2 >= 0 ? i2 : i2 + buffer_for_daq->m_bufferLen;
            SDL_IOprintf(iostream, "%.0f ", buffer_for_daq->get_filtered_sample(ix, -1, 0, 0.0, false, true));
        }
    }
}

void usbCallHandler::drive_daq(int channel, int numToGet, int interval_samples, daqUnitOptions units_sel[2], const char * filepath) {
    LIBRADOR_LOG(LOG_DEBUG, "filepath: %s", filepath);
    SDL_IOStream* iostream = open_file(filepath);
    if((channel == 1) || (channel == 3)) {
        daq_for_channel(1, numToGet, interval_samples, units_sel[0], iostream);
    }
    if(channel == 3) {
        SDL_IOprintf(iostream, "\n");
    }
    if((channel == 2) || (channel == 3)) {
        daq_for_channel(2, numToGet, interval_samples, units_sel[1], iostream);
    }

    SDL_CloseIO(iostream);

    JNIEnv *env = (JNIEnv *) SDL_GetAndroidJNIEnv();
    jobject MainActivityObject = (jobject) SDL_GetAndroidActivity();
    jclass MainActivity(env->GetObjectClass(MainActivityObject));
    jmethodID scanFileID = env->GetMethodID(MainActivity, "scanFile", "(Ljava/lang/String;)V");
    jstring jfilename = env->NewStringUTF(filepath);
    env->CallVoidMethod(MainActivityObject, scanFileID, jfilename);
}

std::vector<double>* usbCallHandler::getMany_double(int channel, int numToGet, double interval_samples, int delay_sample, int filter_mode, bool daq) {
    std::vector<double>* temp_to_return = nullptr;
    if(!daq)
        buffer_read_write_mutex.lock(); //daq uses its own buffer (TODO: same for paused state?)
    int delay_including_trigger;
    bool single_shot_reached = false;
    int trigger_delay = 0;
    switch(deviceMode){
    case 0:
        if(channel == 1) {
            delay_including_trigger = internal_o1_buffer_375_CHA->getDelayIncludingFromTrigger(delay_sample, round(interval_samples * numToGet), daq);
            temp_to_return = internal_o1_buffer_375_CHA->getMany_double(numToGet, interval_samples, delay_including_trigger, filter_mode, current_scope_gain, false, daq);
        }
        break;
    case 1:
        if(channel == 1) {
            delay_including_trigger = internal_o1_buffer_375_CHA->getDelayIncludingFromTrigger(delay_sample, round(interval_samples * numToGet), daq, &single_shot_reached, &trigger_delay);
            internal_o1_buffer_375_CHB->setPaused(single_shot_reached,-trigger_delay, true); // dont multiply trigger_delay by 8 b/c each sample of the buffer is 8 subsamples
            temp_to_return = internal_o1_buffer_375_CHA->getMany_double(numToGet, interval_samples, delay_including_trigger, filter_mode, current_scope_gain, false, daq);
        }
        break;
    case 2:
        if(internal_o1_buffer_375_CHA->isTriggeringEnabled() && ((channel==1) || ((channel == 2) && !internal_o1_buffer_375_CHB->getPaused()))) {
            delay_including_trigger = internal_o1_buffer_375_CHA->getDelayIncludingFromTrigger(delay_sample, round(interval_samples * numToGet), daq, &single_shot_reached, &trigger_delay);
            internal_o1_buffer_375_CHB->setPaused(single_shot_reached,-trigger_delay,true); // only relevant when single-shot triggering
        } else if (internal_o1_buffer_375_CHB->isTriggeringEnabled() && ((channel==2) || ((channel == 1) && !internal_o1_buffer_375_CHA->getPaused()))) {
            delay_including_trigger = internal_o1_buffer_375_CHB->getDelayIncludingFromTrigger(delay_sample, round(interval_samples * numToGet), daq, &single_shot_reached, &trigger_delay);
            internal_o1_buffer_375_CHA->setPaused(single_shot_reached,-trigger_delay,true);// only relevant when single-shot triggering
        } else {
            delay_including_trigger = delay_sample;
        }
        if(channel == 1) temp_to_return = internal_o1_buffer_375_CHA->getMany_double(numToGet, interval_samples, delay_including_trigger, filter_mode, current_scope_gain, false, daq);
        else if (channel == 2) temp_to_return = internal_o1_buffer_375_CHB->getMany_double(numToGet, interval_samples, delay_including_trigger, filter_mode, current_scope_gain, false, daq);
        break;
    case 6:
        if(channel == 1){
            delay_including_trigger = internal_o1_buffer_750->getDelayIncludingFromTrigger(delay_sample, round(interval_samples * numToGet), daq);
            temp_to_return = internal_o1_buffer_750->getMany_double(numToGet, interval_samples, delay_including_trigger, filter_mode, current_scope_gain, false, daq);
        }
        break;
    case 7:
        if(channel == 1) {
            delay_including_trigger = internal_o1_buffer_375_CHA->getDelayIncludingFromTrigger(delay_sample, round(interval_samples * numToGet), daq);
            temp_to_return = internal_o1_buffer_375_CHA->getMany_double(numToGet, interval_samples, delay_including_trigger, filter_mode, current_scope_gain, true, daq);
        }
        break;
    }
    if(!daq)
        buffer_read_write_mutex.unlock();
    return temp_to_return;
}

std::vector<double> * usbCallHandler::getMany_singleBit(int channel, int numToGet, double interval_subsamples, int delay_subsamples, bool daq){
    std::vector<double>* temp_to_return = nullptr;
    if(!daq) // daq uses its own buffer
        buffer_read_write_mutex.lock();
    switch(deviceMode){
    case 1:
        if(channel == 2) 
        {
            int delay_including_trigger;
            if(internal_o1_buffer_375_CHA->isTriggeringEnabled()) {
                bool single_shot_reached = false;
                int trigger_delay = 0;
                delay_including_trigger = internal_o1_buffer_375_CHA->getDelayIncludingFromTrigger(static_cast<int>(round(delay_subsamples/8.)), round(interval_subsamples/8. * numToGet), daq, &single_shot_reached, &trigger_delay) * 8;
                internal_o1_buffer_375_CHB->setPaused(single_shot_reached,-trigger_delay, true); // dont multiply trigger_delay by 8 b/c each sample of the buffer is 8 subsamples
            } else {
                delay_including_trigger = delay_subsamples;
            }

            temp_to_return = internal_o1_buffer_375_CHB->getMany_singleBit(numToGet, interval_subsamples, delay_including_trigger, daq);
            internal_o1_buffer_375_CHB->UartDecode();
        }
        break;
    case 3:
        if(channel == 1)
        {
            temp_to_return = internal_o1_buffer_375_CHA->getMany_singleBit(numToGet, interval_subsamples, delay_subsamples, daq);
            internal_o1_buffer_375_CHA->UartDecode();
        }
        break;
    case 4:
        if(channel == 1){
            temp_to_return = internal_o1_buffer_375_CHA->getMany_singleBit(numToGet, interval_subsamples, delay_subsamples, daq);
            internal_o1_buffer_375_CHA->UartDecode();
        }
        else if (channel == 2){
            temp_to_return = internal_o1_buffer_375_CHB->getMany_singleBit(numToGet, interval_subsamples, delay_subsamples, daq);
            internal_o1_buffer_375_CHB->UartDecode();
        }
        try
        {
            m_i2c_decoder->run();
        }
        catch(...)
        {
            LIBRADOR_LOG(LOG_DEBUG, "Resetting I2C");
            m_i2c_decoder->reset();
        }
        break;
    }
    if(!daq)
        buffer_read_write_mutex.unlock();
    return temp_to_return;
}

char * usbCallHandler::getUart_String(int channel, bool* parity_check)
{
    char * temp_to_return = nullptr;
    if(channel == 1)
        temp_to_return = internal_o1_buffer_375_CHA->getUart_String(parity_check);
    else if (channel == 2)
        temp_to_return = internal_o1_buffer_375_CHB->getUart_String(parity_check);
    return temp_to_return;
}

std::vector<double> *usbCallHandler::getMany_sincelast(int channel, int feasible_window_begin, int feasible_window_end, int interval_samples, int filter_mode){
    std::vector<double>* temp_to_return = nullptr;
    buffer_read_write_mutex.lock();
        switch(deviceMode){
        case 0:
            if(channel == 1) temp_to_return = internal_o1_buffer_375_CHA->getSinceLast(feasible_window_begin, feasible_window_end, interval_samples, filter_mode, current_scope_gain, false);
            break;
        case 1:
            if(channel == 1) temp_to_return = internal_o1_buffer_375_CHA->getSinceLast(feasible_window_begin, feasible_window_end, interval_samples, filter_mode, current_scope_gain, false);
            break;
        case 2:
            if(channel == 1) temp_to_return = internal_o1_buffer_375_CHA->getSinceLast(feasible_window_begin, feasible_window_end, interval_samples, filter_mode, current_scope_gain, false);
            else if (channel == 2) temp_to_return = internal_o1_buffer_375_CHB->getSinceLast(feasible_window_begin, feasible_window_end, interval_samples, filter_mode, current_scope_gain, false);
            break;
        case 6:
            if(channel == 1) temp_to_return = internal_o1_buffer_750->getSinceLast(feasible_window_begin, feasible_window_end, interval_samples, filter_mode, current_scope_gain, false);
            break;
        case 7:
            if(channel == 1) temp_to_return = internal_o1_buffer_375_CHA->getSinceLast(feasible_window_begin, feasible_window_end, interval_samples, filter_mode, current_scope_gain, false);
            break;
        }
        buffer_read_write_mutex.unlock();
        return temp_to_return;

}

int usbCallHandler::send_device_reset(){
    libusb_reset_device(handle);
    return 0;
}


int usbCallHandler::set_device_mode(int mode){
    if((mode < 0) || (mode > MAX_SUPPORTED_DEVICE_MODE)){
        return -1;
    }
    deviceMode = mode;
    send_control_transfer_with_error_checks(0x40, 0xa5, (mode == 5 ? 0 : mode), gainMask, 0, nullptr);

    send_function_gen_settings(1);
    send_function_gen_settings(2);

    internal_o1_buffer_375_CHA->reset(false);
    internal_o1_buffer_375_CHA->resetTrigger(current_scope_gain, deviceMode==7);
    internal_o1_buffer_375_CHB->reset(false);
    internal_o1_buffer_375_CHB->resetTrigger(current_scope_gain, deviceMode==7);
    internal_o1_buffer_750->reset(false);
    internal_o1_buffer_750->resetTrigger(current_scope_gain, deviceMode==7);

    return 0;
}

int usbCallHandler::set_gain(double newGain){
    //See XMEGA_AU Manual, page 359.  ADC.CTRL.GAIN.
    if(newGain==0.5) gainMask = 0x07;
    else if (newGain == 1) gainMask = 0x00;
    else if (newGain == 2) gainMask = 0x01;
    else if (newGain == 4) gainMask = 0x02;
    else if (newGain == 8) gainMask = 0x03;
    else if (newGain == 16) gainMask = 0x04;
    else if (newGain == 32) gainMask = 0x05;
    else if (newGain == 64) gainMask = 0x06;
    else{
      LIBRADOR_LOG(LOG_ERROR, "Invalid gain value.  Valid values are 0.1, 1, 2, 4, 8, 16, 32, 64\n");
      return -1;
    }

    gainMask = gainMask << 2;
    gainMask |= (gainMask << 8);
    send_control_transfer_with_error_checks(0x40, 0xa5, deviceMode, gainMask, 0, nullptr);
    current_scope_gain = newGain;
    return 0;
}

int usbCallHandler::update_function_gen_settings(int channel, unsigned char *sampleBuffer, int numSamples, double usecs_between_samples, double amplitude_v, double offset_v){


    //Parse the channel
    fGenSettings *fGenSelected;
    if(channel == 1){
        fGenSelected = &functionGen_CH1;
    } else if (channel == 2){
        fGenSelected = &functionGen_CH2;
    } else {
        return -1;  //Invalid channel
    }

    //Update number of samples.
    fGenSelected->numSamples = numSamples;

    //Does the output amplifier need to be enabled?
    if ((amplitude_v+offset_v) > FGEN_LIMIT){
        amplitude_v = amplitude_v / 3;
        offset_v = offset_v / 3;
        if(channel == 1){
            fGenTriple |= 0b00000001;
        } else {
            fGenTriple |= 0b00000010;
        }
    }
    else {
        if(channel == 1){
            fGenTriple &= 0b11111110;
        } else {
            fGenTriple &= 0b11111101;
        }
    }

    //Fiddle with the waveform to deal with the fact that the Xmega has a minimum DAC output value.
    double amplitude_sample = (amplitude_v * 255) / FGEN_LIMIT;
    double offset_sample = (offset_v * 255) / FGEN_LIMIT;
    if (offset_sample < FGEN_SAMPLE_MIN){  //If the offset is in the range where the XMEGA output cannot physically drive the signal that low...
        if (amplitude_sample > FGEN_SAMPLE_MIN){  //And the amplitude of the signal can go above this minimum range
            amplitude_sample -= FGEN_SAMPLE_MIN;  //Then reduce the amplitude and add a small offset
            }
        else {
            amplitude_sample = 0;
        }
        offset_sample = FGEN_SAMPLE_MIN;
    }

    //Apply amplitude and offset scaling to all samples.
    double tempDouble;
    for (int i=0;i<numSamples;i++){
        tempDouble = (double) sampleBuffer[i];
        tempDouble *= amplitude_sample;
        tempDouble /= 255.0;
        tempDouble += offset_sample;
        fGenSelected->samples[i] = (uint8_t) tempDouble;
    }

    //Calculate timer values
    int validClockDivs[7] = {1, 2, 4, 8, 64, 256, 1024};

    int clkSetting;
    for(clkSetting = 0; clkSetting<7; clkSetting++){
        if ( ((XMEGA_MAIN_FREQ * usecs_between_samples)/(1000000 * validClockDivs[clkSetting])) < 65535)
             break;
    }
    fGenSelected->timerPeriod = (usecs_between_samples * XMEGA_MAIN_FREQ) / (1000000 * validClockDivs[clkSetting]) - 0.5;
    fGenSelected->clockDividerSetting = clkSetting + 1;

    return 0;
}

int usbCallHandler::send_function_gen_settings(int channel){
    if(channel == 1){
        if(functionGen_CH1.numSamples == 0){
            return -1; //Channel not initialised
        }
        send_control_transfer_with_error_checks(0x40, 0xa2, functionGen_CH1.timerPeriod, functionGen_CH1.clockDividerSetting, functionGen_CH1.numSamples, functionGen_CH1.samples);
    } else if (channel == 2){
        if(functionGen_CH2.numSamples == 0){
            return -1; //Channel not initialised
        }
        send_control_transfer_with_error_checks(0x40, 0xa1, functionGen_CH2.timerPeriod, functionGen_CH2.clockDividerSetting, functionGen_CH2.numSamples, functionGen_CH2.samples);
    } else {
        return -2; //Invalid channel
    }
    send_control_transfer_with_error_checks(0x40, 0xa4, fGenTriple, 0, 0, nullptr);
    return 0;
}

int usbCallHandler::set_psu_voltage(double voltage){
    double vinp = voltage/11;
    double vinn = 0;

    uint8_t dutyPsu = (uint8_t) ((vinp - vinn)/vref_psu * gain_psu * PSU_ADC_TOP);


    if ((dutyPsu>106) || (dutyPsu<21)){
        return -1;  //Out of range
    }
    send_control_transfer_with_error_checks(0x40, 0xa3, dutyPsu, 0, 0, nullptr);
    return 0;
}

int usbCallHandler::set_digital_state(uint8_t digState){
    send_control_transfer_with_error_checks(0x40, 0xa6, digState, 0, 0, nullptr);
    return 0;
}

int usbCallHandler::reset_device(bool goToBootloader){
    LIBRADOR_LOG(LOG_DEBUG, "resetting device: unimportant error LIBUSB_ERROR_NO_DEVICE for send_control_transfer expected"); // only if goToBootloader?
    send_control_transfer_with_error_checks(0x40, 0xa7, (goToBootloader ? 1 : 0), 0, 0, nullptr);
    return 0;
}

uint16_t usbCallHandler::get_firmware_version(){
    send_control_transfer_with_error_checks(0xc0, 0xa8, 0, 0, 2, nullptr);
    return *((uint16_t *) inBuffer);
}

uint8_t usbCallHandler::get_firmware_variant(){
    send_control_transfer_with_error_checks(0xc0, 0xa9, 0, 0, 1, nullptr);
    return *((uint8_t *) inBuffer);
}

double usbCallHandler::get_samples_per_second(){
    switch(deviceMode){
    case 0:
        return (double)(375000.0);
    case 1:
        return (double)(375000.0);
    case 2:
        return (double)(375000.0);
    case 3:
        return (double)(375000.0);
    case 4:
        return (double)(375000.0);
    case 6:
        return (double)(750000.0);
    case 7:
        return (double)(375000.0);
    default:
        return 0;
    }
}

int usbCallHandler::set_synchronous_pause_state(bool newState){
    if(newState && !synchronous_pause_state){
        buffer_read_write_mutex.lock();
        synchronous_pause_state = true;
        return 0;
    }

    if(!newState && synchronous_pause_state){
        buffer_read_write_mutex.unlock();
        return 0;
    }

    //Otherwise you don't want to do anything.  You should never set the state twice.
    return 1;
}

int usbCallHandler::setPaused(int channel, bool is_paused)
{
    if(channel==1){
        if(deviceMode==6)
            return internal_o1_buffer_750->setPaused(is_paused);
        else
            return internal_o1_buffer_375_CHA->setPaused(is_paused);
    } else if (channel == 2){
        return internal_o1_buffer_375_CHB->setPaused(is_paused);
    }
    return 1;
}

bool usbCallHandler::getPaused(int channel)
{
    if(channel==1){
        if(deviceMode==6)
            return internal_o1_buffer_750->getPaused();
        else
            return internal_o1_buffer_375_CHA->getPaused();
    } else if (channel == 2){
        return internal_o1_buffer_375_CHB->getPaused();
    }
    return 1;
}

void usbCallHandler::setTriggerSettings(int channel, o1buffer::trigger_settings new_trigger_settings)
{
    setSettingsForChannel(channel, new_trigger_settings, internal_o1_buffer_375_CHA, internal_o1_buffer_375_CHB, internal_o1_buffer_750, &o1buffer::setTriggerSettings);
}

void usbCallHandler::setVirtualTransformSettings(int channel, o1buffer::virtual_transform_settings new_virtual_transform_settings)
{
    setSettingsForChannel(channel, new_virtual_transform_settings, internal_o1_buffer_375_CHA, internal_o1_buffer_375_CHB, internal_o1_buffer_750, &o1buffer::setVirtualTransformSettings);
}

void usbCallHandler::setUartDecodeSettings(int channel, UartSettings new_settings)
{
    if(channel==1)
        internal_o1_buffer_375_CHA->setUartDecodeSettings(new_settings);
    else if (channel==2)
        internal_o1_buffer_375_CHB->setUartDecodeSettings(new_settings);
}
 char *usbCallHandler::getI2c_String()
{
    return m_i2c_decoder->getString();
}

void usbCallHandler::setI2cIsDecoding(bool new_decode_on){
    m_i2c_decoder->setIsDecoding(new_decode_on);
}

bool usbCallHandler::isoThreadIsActive(){
    return iso_thread_active;
}

#ifdef PLATFORM_ANDROID
int usbCallHandler::flashFirmware(int file_descriptor){

    libusb_device *device_ptr;

    init_libusb();
    int error = setup_usb_control(file_descriptor);

    JNIEnv *env = (JNIEnv *) SDL_GetAndroidJNIEnv();
    jobject MainActivityObject = (jobject) SDL_GetAndroidActivity();
    jclass MainActivity(env->GetObjectClass(MainActivityObject));

    jfieldID asset_manager_id = env->GetFieldID(MainActivity, "mgr", "Landroid/content/res/AssetManager;");
    jobject mgr_java = (jobject)env->GetObjectField(MainActivityObject, asset_manager_id);
    const char* external_filepath = SDL_GetAndroidExternalStoragePath();
    AAssetManager * mgr = AAssetManager_fromJava(env, mgr_java);

    const char* firmware_filename = "labrafirm_0007_02.hex";
    char apk_firmware_filepath[64];
    strcpy(apk_firmware_filepath, "firmware/");
    strcat(apk_firmware_filepath, firmware_filename);

    char firmware_copy_filepath[256];
    strcpy(firmware_copy_filepath, external_filepath);
    strcat(firmware_copy_filepath, "/");
    strcat(firmware_copy_filepath, firmware_filename);
    LIBRADOR_LOG(LOG_DEBUG, "firmware copy path: %s", firmware_copy_filepath);

    AAsset* asset = AAssetManager_open(mgr, apk_firmware_filepath, AASSET_MODE_STREAMING);
    char buf[2048];
    int nb_read = 0;
    FILE* out = fopen(firmware_copy_filepath, "w+");
    while ((nb_read = AAsset_read(asset, buf, 2048)) > 0)
        fwrite(buf, nb_read, 1, out);
    fclose(out);
    AAsset_close(asset);

    LIBRADOR_LOG(LOG_DEBUG, "FLASHING %s", firmware_copy_filepath);

    //Set up interface to dfuprog
    int exit_code;
    char command[256];

    //Run stage 1
    LIBRADOR_LOG(LOG_DEBUG, "\n\nFlashing Firmware, stage 1.\n\n");
    snprintf(command, sizeof command, "dfu-programmer atxmega32a4u erase --force --debug 300");
    exit_code = dfuprog_virtual_cmd(command, device_ptr, handle, ctx, 0);
    if (exit_code) {
        LIBRADOR_LOG(LOG_WARNING, "ERROR ERASING FIRMWARE.");
    } else {
        LIBRADOR_LOG(LOG_DEBUG, "ERASED FIRMWARE.");
    }
    ctx = nullptr;
    handle=  nullptr;

    init_libusb();
    error = setup_usb_control(file_descriptor);

    //Run stage 2
    LIBRADOR_LOG(LOG_DEBUG, "\n\nFlashing Firmware, stage 2.\n\n");
    snprintf(command, sizeof command, "dfu-programmer atxmega32a4u flash %s --debug 300", firmware_copy_filepath);
    exit_code = dfuprog_virtual_cmd(command, device_ptr, handle, ctx, 0);
    if (exit_code) {
        LIBRADOR_LOG(LOG_WARNING, "\n\n\nERROR WRITING NEW FIRMWARE TO DEVICE.\n\n\n");
        //return exit_code+200;
    }
    ctx = nullptr;
    handle=  nullptr;

    init_libusb();
    error = setup_usb_control(file_descriptor);

    //Run stage 3
    LIBRADOR_LOG(LOG_DEBUG, "\n\nFlashing Firmware, stage 3.\n\n");
    dfu_launch();
    starting_after_flash = true;
    return 0;
}
#endif

void usbCallHandler::dfu_launch() {
    LIBRADOR_LOG(LOG_DEBUG, "\n\n\nDFU LAUNCH.\n\n\n");
    int exit_code;
    char command[256];
    libusb_device *device_ptr;
    snprintf(command, sizeof command, "dfu-programmer atxmega32a4u launch");
    exit_code = dfuprog_virtual_cmd(command, device_ptr, handle, ctx, 0);
    if (exit_code) {
        LIBRADOR_LOG(LOG_WARNING, "\n\n\n DFU LAUNCH ERROR\n\n\n");
        //return exit_code+300;
    }
    ctx = nullptr;
    handle=  nullptr;
}

#ifdef PLATFORM_ANDROID
void usbCallHandler::respondToStartupOrUsbStateChange(bool is_plugged_in, int file_descriptor, bool bootloader_mode){
    if(is_plugged_in) {
        if(bootloader_mode) {
            LIBRADOR_LOG(LOG_DEBUG, "found in bootloader mode");
            if(starting_after_flash) {
                LIBRADOR_LOG(LOG_DEBUG, "startup after flash");
                starting_after_flash = false;
                init_libusb();
                int error = setup_usb_control(file_descriptor);
                set_bootloader_mode_allowed(false);
                dfu_launch();// launch twice to clear eeprom flag after flashing

                JNIEnv *env = (JNIEnv *) SDL_GetAndroidJNIEnv();
                jobject MainActivityObject = (jobject) SDL_GetAndroidActivity();
                jclass MainActivity(env->GetObjectClass(MainActivityObject));
                jmethodID confirmFirmwareFlashID = env->GetMethodID(MainActivity, "confirmFirmwareFlash", "()V");
                env->CallVoidMethod(MainActivityObject,confirmFirmwareFlashID);
            } else {
                int flashRet = flashFirmware(file_descriptor);
                LIBRADOR_LOG(LOG_DEBUG, "flashRet: %d", flashRet);
            }
        } else {
            if(connected) {
                return;
            }
            init_libusb();
            int control_setup_success = setup_usb_control(file_descriptor);
            if(control_setup_success==0) {
                connected = true;
                uint16_t firmver = get_firmware_version();
                LIBRADOR_LOG(LOG_DEBUG, "BOARD IS RUNNING FIRMWARE VERSION 0x%04hx", firmver);
                LIBRADOR_LOG(LOG_DEBUG, "EXPECTING FIRMWARE VERSION 0x%04hx", EXPECTED_FIRMWARE_VERSION);

                uint8_t variant = get_firmware_variant();
                LIBRADOR_LOG(LOG_DEBUG, "FIRMWARE VARIANT = 0x%02hx", variant);
                LIBRADOR_LOG(LOG_DEBUG, "EXPECTED VARIANT = 0x%02hx", DEFINED_EXPECTED_VARIANT);

                if((firmver != EXPECTED_FIRMWARE_VERSION) || (variant != DEFINED_EXPECTED_VARIANT)){
                    LIBRADOR_LOG(LOG_DEBUG, "Unexpected Firmware!!");

                    JNIEnv *env = (JNIEnv *) SDL_GetAndroidJNIEnv();
                    jobject MainActivityObject = (jobject) SDL_GetAndroidActivity();
                    jclass MainActivity(env->GetObjectClass(MainActivityObject));
                    jmethodID requestFirmwareFlashID = env->GetMethodID(MainActivity, "requestFirmwareFlash", "()V");
                    env->CallVoidMethod(MainActivityObject,requestFirmwareFlashID);

                    return;
                } else {
                    int iso_setup_success = setup_usb_iso();
                    return;
                }
            } else {
                connected = false;
                if(handle){
                    libusb_release_interface(handle, 0);
                    libusb_close(handle);
                    handle = nullptr;
                }
                return;
            }
        }
    } else {
        connected = false;
        if(iso_polling_thread) {
            begin_iso_thread_shutdown();
            iso_polling_thread->join();
            delete iso_polling_thread;

//             prepare for possible replug
            iso_thread_shutdown_requested = false;
            iso_polling_thread = nullptr;
            iso_thread_shutdown_remaining_transfers = NUM_FUTURE_CTX;
            for (int i=0; i<NUM_FUTURE_CTX; i++){
                for (int k=0; k<NUM_ISO_ENDPOINTS; k++){
                    libusb_free_transfer(isoCtx[k][i]);
                }
            }
        }
        if(handle){
            libusb_release_interface(handle, 0);
            LIBRADOR_LOG(LOG_DEBUG, "Interface released\n");
            libusb_close(handle);
            handle = nullptr;
            LIBRADOR_LOG(LOG_DEBUG, "Device Closed\n");
        }
    }
}

SDL_IOStream* usbCallHandler::open_file(const char * filepath) {
    JNIEnv *env = (JNIEnv *) SDL_GetAndroidJNIEnv();
    jobject MainActivityObject = (jobject) SDL_GetAndroidActivity();
    jclass MainActivity(env->GetObjectClass(MainActivityObject));
    jmethodID initFileID = env->GetMethodID(MainActivity, "initFile", "(Ljava/lang/String;)Ljava/lang/String;");
    LIBRADOR_LOG(LOG_DEBUG, "daq filename: %s", filepath);
    jstring jfilename = env->NewStringUTF(filepath);
    jstring juri = (jstring)env->CallObjectMethod(MainActivityObject, initFileID, jfilename);
    const char *uri = env->GetStringUTFChars(juri, 0);
    LIBRADOR_LOG(LOG_DEBUG, "daq uri: %s", uri);
    env->DeleteLocalRef(jfilename);
// same as in SDL/src/io/SDL_iostream.c but using a file:// uri b/c such uris are valid inputs to ContentResolver:openFileDescriptor (which gets called by SDL via JNI in SDLActivity.java:openFileDescriptor)
    int fd = Android_JNI_OpenFileDescriptor(uri, "w");
    FILE *fp = fdopen(fd, "w");
    SDL_IOStream* iostream = SDL_IOFromFP(fp, true);
    return iostream;
}
#endif


