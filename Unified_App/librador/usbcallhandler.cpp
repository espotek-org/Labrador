#include "usbcallhandler.h"
//#include <stdio.h>

#include <math.h>
#include "logging_internal.h"
#include "uartstyledecoder.h"
#include <mutex>
#include <thread>

#include <SDL3/SDL_iostream.h>
#include "librador_platform.h"

// ---------------------------------------------------------------------------
// Frame ingestion.  All transports normalise to 750-byte frames dispatched
// into the o1buffers by device mode, with validity accounting fed by the
// per-frame headers (see the AIO_* notes in usbcallhandler.h).
// ---------------------------------------------------------------------------

void usbCallHandler::dispatch_frame(unsigned char *frame750){
    buffer_read_write_mutex.lock();
    switch(deviceMode){
    case 0:
        internal_o1_buffer_375_CHA->addVector((char*) frame750, 375);
        break;
    case 1:
        internal_o1_buffer_375_CHA->addVector((char*) frame750, 375);
        internal_o1_buffer_375_CHB->addVector((unsigned char*) &frame750[375], 375);
        break;
    case 2:
        internal_o1_buffer_375_CHA->addVector((char*) frame750, 375);
        internal_o1_buffer_375_CHB->addVector((char*) &frame750[375], 375);
        break;
    case 3:
        internal_o1_buffer_375_CHA->addVector((unsigned char*) frame750, 375);
        break;
    case 4:
        internal_o1_buffer_375_CHA->addVector((unsigned char*) frame750, 375);
        internal_o1_buffer_375_CHB->addVector((unsigned char*) &frame750[375], 375);
        break;
    case 6:
        internal_o1_buffer_750->addVector((char*) frame750, 750);
        break;
    case 7:
        internal_o1_buffer_375_CHA->addVector((short*) frame750, 375);
        break;
    }
    buffer_read_write_mutex.unlock();
}

static unsigned char xor_csum_750(const unsigned char *p){
    unsigned char c = 0;
    for(int i = 0; i < ISO_PACKET_SIZE; i++) c ^= p[i];
    return c;
}

void usbCallHandler::note_frame_csum(unsigned char csum, bool valid){
    frame_csum_ring[data_frame_counter % CSUM_RING_SIZE] = csum;
    frame_csum_valid[data_frame_counter % CSUM_RING_SIZE] = valid;
    data_frame_counter++;
}

void usbCallHandler::note_seq(uint16_t seq){
    if(seq_started){
        uint16_t expect = (uint16_t)(last_seq + 1);
        if(seq != expect){
            frames_dropped += (uint16_t)(seq - expect);
        }
    }
    seq_started = true;
    last_seq = seq;
}

void usbCallHandler::reset_frame_state(){
    seq_started = false;
    data_frame_counter = 0;
    meta_frame_counter = 0;
    bulk_stream.clear();
    for(int i = 0; i < CSUM_RING_SIZE; i++) frame_csum_valid[i] = false;
    for(int k = 0; k < MAX_DATA_ENDPOINTS; k++) iso6_queues[k].clear();
}

void usbCallHandler::reset_frame_stats(){
    frames_ok = 0;
    frames_bad_checksum = 0;
    frames_dropped = 0;
    frames_unvalidated = 0;
}

void usbCallHandler::get_frame_stats(uint64_t *ok, uint64_t *bad_checksum, uint64_t *dropped, uint64_t *unvalidated){
    if(ok) *ok = frames_ok.load();
    if(bad_checksum) *bad_checksum = frames_bad_checksum.load();
    if(dropped) *dropped = frames_dropped.load();
    if(unvalidated) *unvalidated = frames_unvalidated.load();
}

// iso1 data packet: one packet per frame, 750 bytes when healthy.
void usbCallHandler::handle_data_iso_packet(unsigned char *data, int length){
    if(length == ISO_PACKET_SIZE){
        note_frame_csum(xor_csum_750(data), true);
        dispatch_frame(data);
    } else {
        // Device skipped this frame (or partial) - hole in the stream.
        note_frame_csum(0, false);
        if(length != 0) frames_bad_checksum++;
    }
}

// iso6: queue per-endpoint packets, assemble frames in lockstep once all
// six endpoints have delivered their slice of the frame.
void usbCallHandler::handle_iso6_packet(int ep_index, unsigned char *data, int length){
    iso6_queues[ep_index].emplace_back(data, data + length);
    // Bound the queues in case one endpoint stalls permanently.
    if(iso6_queues[ep_index].size() > 4 * ISO_PACKETS_PER_CTX){
        iso6_queues[ep_index].pop_front();
    }
}

void usbCallHandler::drain_iso6_queues(){
    while(true){
        for(int k = 0; k < AIO_EP_ISO6_COUNT; k++){
            if(iso6_queues[k].empty()) return;
        }
        unsigned char frame[ISO_PACKET_SIZE];
        bool complete = true;
        int off = 0;
        for(int k = 0; k < AIO_EP_ISO6_COUNT; k++){
            std::vector<unsigned char> &pkt = iso6_queues[k].front();
            if(pkt.size() != ISO_PACKET_SIZE / AIO_EP_ISO6_COUNT){
                complete = false;
            } else if(complete){
                memcpy(&frame[off], pkt.data(), pkt.size());
                off += (int)pkt.size();
            }
            iso6_queues[k].pop_front();
        }
        if(complete){
            note_frame_csum(xor_csum_750(frame), true);
            dispatch_frame(frame);
        } else {
            note_frame_csum(0, false);
        }
    }
}

// Meta packet (iso transports): EB 58 seqL seqH csum_half0 csum_half1
// usb_state mode, describing the previous frame (lag-1).  Because the meta
// and data URB streams can start a frame or two apart, each meta is matched
// against a small window of recent frame checksums.
void usbCallHandler::handle_meta_packet(unsigned char *data, int length){
    meta_frame_counter++;
    if(length == 0) return;   // device had no meta armed that frame
    if(length != AIO_META_PACKET_SIZE || data[0] != AIO_HDR_MAGIC0 || data[1] != AIO_HDR_MAGIC1_META){
        frames_bad_checksum++;  // corrupt meta counts against validity
        return;
    }
    note_seq((uint16_t)(data[2] | (data[3] << 8)));
    unsigned char c0 = data[4];
    unsigned char c1 = data[5];
    // lag-1 nominal: meta in frame N describes data frame N-1
    bool matched = false;
    bool any_candidate = false;
    for(long lag = -3; lag <= 1; lag++){
        long idx = (long)meta_frame_counter - 1 + lag;
        if(idx < 0 || idx >= (long)data_frame_counter) continue;
        if((long)data_frame_counter - idx > CSUM_RING_SIZE) continue;
        int slot = idx % CSUM_RING_SIZE;
        if(!frame_csum_valid[slot]) continue;
        any_candidate = true;
        if(frame_csum_ring[slot] == c0 || frame_csum_ring[slot] == c1){
            matched = true;
            break;
        }
    }
    if(matched) frames_ok++;
    else if(any_candidate) frames_bad_checksum++;
    else frames_unvalidated++;
}

// Bulk stream parser: [EB 57 seqL seqH lenL lenH csum mode] + payload.
void usbCallHandler::handle_bulk_bytes(unsigned char *data, int length){
    bulk_stream.insert(bulk_stream.end(), data, data + length);
    size_t pos = 0;
    while(true){
        while(bulk_stream.size() - pos >= 2 &&
              !(bulk_stream[pos] == AIO_HDR_MAGIC0 && bulk_stream[pos+1] == AIO_HDR_MAGIC1_BULK)){
            pos++;
        }
        if(bulk_stream.size() - pos < 8) break;
        uint16_t len = bulk_stream[pos+4] | (bulk_stream[pos+5] << 8);
        if(len != ISO_PACKET_SIZE){ pos++; continue; }
        if(bulk_stream.size() - pos < (size_t)(8 + len)) break;
        uint16_t seq = bulk_stream[pos+2] | (bulk_stream[pos+3] << 8);
        unsigned char csum = bulk_stream[pos+6];
        unsigned char *payload = bulk_stream.data() + pos + 8;
        note_seq(seq);
        if(xor_csum_750(payload) == csum){
            frames_ok++;
            dispatch_frame(payload);
        } else {
            // Stomped mid-flight by the ADC/DMA loop - drop it rather than
            // pollute the scope buffers with torn data.
            frames_bad_checksum++;
        }
        pos += 8 + len;
    }
    bulk_stream.erase(bulk_stream.begin(), bulk_stream.begin() + pos);
}

// ---------------------------------------------------------------------------
// libusb async callbacks (event-loop thread)
// ---------------------------------------------------------------------------

void usbCallHandler::rearm_or_retire(struct libusb_transfer *transfer){
    if(!is_iso_thread_shutdown_requested()){
        int error = libusb_submit_transfer(transfer);
        if(error){
            LIBRADOR_LOG(LOG_WARNING, "Error re-arming the endpoint!\n");
            begin_iso_thread_shutdown();
            decrement_remaining_transfers();
            LIBRADOR_LOG(LOG_WARNING, "Transfer not being rearmed!  %d armed transfers remaining\n", iso_thread_shutdown_remaining_transfers);
        }
    } else {
        decrement_remaining_transfers();
        LIBRADOR_LOG(LOG_WARNING, "Transfer not being rearmed!  %d armed transfers remaining\n", iso_thread_shutdown_remaining_transfers);
    }
}

void LIBUSB_CALL isoCallback(struct libusb_transfer * transfer){
    usbCallHandler *usb_driver = (usbCallHandler *)transfer->user_data;
    if(transfer->status==LIBUSB_TRANSFER_COMPLETED)
    {
        // Which data endpoint is this?
        int ep_index = 0;
        if(usb_driver->active_transport == LABRADOR_TRANSPORT_ISO6){
            ep_index = transfer->endpoint - AIO_EP_ISO6_FIRST;
        }
        for(int i=0;i<transfer->num_iso_packets;i++){
            unsigned char *packetPointer = libusb_get_iso_packet_buffer_simple(transfer, i);
            int actual = transfer->iso_packet_desc[i].actual_length;
            if(transfer->iso_packet_desc[i].status != LIBUSB_TRANSFER_COMPLETED){
                actual = 0;
            }
            if(usb_driver->active_transport == LABRADOR_TRANSPORT_ISO6){
                usb_driver->handle_iso6_packet(ep_index, packetPointer, actual);
            } else {
                usb_driver->handle_data_iso_packet(packetPointer, actual);
            }
        }
        if(usb_driver->active_transport == LABRADOR_TRANSPORT_ISO6){
            usb_driver->drain_iso6_queues();
        }
    }
    usb_driver->rearm_or_retire(transfer);
}

void LIBUSB_CALL metaIsoCallback(struct libusb_transfer * transfer){
    usbCallHandler *usb_driver = (usbCallHandler *)transfer->user_data;
    if(transfer->status==LIBUSB_TRANSFER_COMPLETED)
    {
        for(int i=0;i<transfer->num_iso_packets;i++){
            unsigned char *packetPointer = libusb_get_iso_packet_buffer_simple(transfer, i);
            int actual = transfer->iso_packet_desc[i].actual_length;
            if(transfer->iso_packet_desc[i].status != LIBUSB_TRANSFER_COMPLETED){
                actual = 0;
            }
            usb_driver->handle_meta_packet(packetPointer, actual);
        }
    }
    usb_driver->rearm_or_retire(transfer);
}

void LIBUSB_CALL bulkCallback(struct libusb_transfer * transfer){
    usbCallHandler *usb_driver = (usbCallHandler *)transfer->user_data;
    if(transfer->status==LIBUSB_TRANSFER_COMPLETED || transfer->status==LIBUSB_TRANSFER_TIMED_OUT)
    {
        if(transfer->actual_length > 0){
            usb_driver->handle_bulk_bytes(transfer->buffer, transfer->actual_length);
        }
    }
    usb_driver->rearm_or_retire(transfer);
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
    auto last_stats = std::chrono::steady_clock::now();
    while(!safe_to_exit_thread()){
        //printf("iso_polling_function begin loop\n");
        if(libusb_event_handling_ok(ctx)){
            libusb_handle_events_timeout(ctx, &tv);
        }
        auto now = std::chrono::steady_clock::now();
        if(now - last_stats > std::chrono::seconds(10)){
            last_stats = now;
            LIBRADOR_LOG(LOG_DEBUG, "frame stats: ok=%llu bad_csum=%llu dropped=%llu unvalidated=%llu (transport %d)\n",
                (unsigned long long)frames_ok.load(), (unsigned long long)frames_bad_checksum.load(),
                (unsigned long long)frames_dropped.load(), (unsigned long long)frames_unvalidated.load(),
                active_transport);
        }
    }
    iso_thread_active = false;
    LIBRADOR_LOG(LOG_DEBUG, "iso_polling_function thread finished\n");
}

usbCallHandler::usbCallHandler(unsigned short VID_in, unsigned short PID_in)
{
    VID = VID_in;
    PID = PID_in;

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

        free_transfers();
    }

    if(daq_thread && daq_thread->joinable()){
        daq_thread->join();
    }

    if(handle){
        if(claimed_iface > 0){
            libusb_release_interface(handle, claimed_iface);
        }
        if(iface0_claimed){
            libusb_release_interface(handle, 0);
        }
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

void usbCallHandler::set_bootloader_mode_allowed(bool allowed) {
    if(librador_get_host_hooks().set_bootloader_mode_allowed)
        librador_get_host_hooks().set_bootloader_mode_allowed(allowed);
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

//Initialise libusb
int usbCallHandler::init_libusb(){
    if(ctx){
        // Called every device poll on desktop — stay quiet when already up
        return 1;
    } else LIBRADOR_LOG(LOG_DEBUG, "libusb context is null\n");
#ifdef PLATFORM_ANDROID
    // Android hands us an fd from UsbManager; libusb must not enumerate itself.
    struct libusb_init_option libusb_options[2] = {
        {.option = LIBUSB_OPTION_NO_DEVICE_DISCOVERY},
        {.option = LIBUSB_OPTION_LOG_LEVEL, .value = {.ival = 3}}
    };
    int error = libusb_init_context(&ctx, libusb_options, 2);
#else
    struct libusb_init_option libusb_options[1] = {
        {.option = LIBUSB_OPTION_LOG_LEVEL, .value = {.ival = 3}}
    };
    int error = libusb_init_context(&ctx, libusb_options, 1);
#endif
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
    return claim_and_prepare();
}

int usbCallHandler::claim_and_prepare(){
#ifdef __APPLE__
    // Do NOT claim any interface before the firmware check.  Device-level
    // control transfers work unclaimed on Darwin, and claiming interface 0
    // on a board still running pre-AIO firmware (iso endpoints in alt
    // setting 0) kernel-panics macOS Tahoe.  The streaming interface is
    // claimed in start_streaming() once the firmware is known to be AIO.
    iface0_claimed = false;
    return 0;
#else
    if(libusb_kernel_driver_active(handle, 0)) {
        LIBRADOR_LOG(LOG_DEBUG, "KERNEL DRIVER ACTIVE");
    } else {
        LIBRADOR_LOG(LOG_DEBUG, "KERNEL DRIVER INACTIVE");
    }
    if(libusb_kernel_driver_active(handle, 0)){
        libusb_detach_kernel_driver(handle, 0);
    }

    //Claim interface 0.  On AIO firmware its default alt setting has no
    //endpoints, so this is inert; it exists to keep control transfers
    //working on platforms that expect a claimed interface (WinUSB).
    int error = libusb_claim_interface(handle, 0);
    if(error){
        LIBRADOR_LOG(LOG_WARNING, "libusb_claim_interface FAILED\n");
        libusb_close(handle);
        handle = nullptr;
        return -3;
    } else LIBRADOR_LOG(LOG_DEBUG, "Interface claimed!\n");
    iface0_claimed = true;
    return 0;
#endif
}

int usbCallHandler::set_transport(int transport){
    if(transport < LABRADOR_TRANSPORT_AUTO || transport > LABRADOR_TRANSPORT_BULK){
        return -1;
    }
    requested_transport = transport;
    return 0;
}

int usbCallHandler::get_active_transport(){
    return active_transport;
}

int usbCallHandler::resolve_transport(){
    int t = requested_transport;
    // Developer/test override; same values as LABRADOR_TRANSPORT_*.
    const char *env = getenv("LABRADOR_TRANSPORT");
    if(env && env[0] >= '1' && env[0] <= '3' && env[1] == '\0'){
        t = env[0] - '0';
        LIBRADOR_LOG(LOG_WARNING, "transport overridden to %d via LABRADOR_TRANSPORT\n", t);
    }
    if(t == LABRADOR_TRANSPORT_AUTO){
#if defined(__APPLE__)
        t = LABRADOR_TRANSPORT_BULK;
#elif defined(_WIN64)
        t = LABRADOR_TRANSPORT_ISO6;
#elif defined(_WIN32)
        t = LABRADOR_TRANSPORT_ISO1;
#else
        t = LABRADOR_TRANSPORT_ISO1;   // linux (all), android, pi
#endif
    }
#if defined(__APPLE__) && !defined(LIBRADOR_MACOS_ALLOW_ISO)
    // Opening a full-speed isochronous pipe kernel-panics macOS Tahoe
    // (IOUSBHostFamily's getEndpointMult dereferences the NULL SuperSpeed
    // companion descriptor for full-speed iso endpoints).  Bulk only.
    if(t != LABRADOR_TRANSPORT_BULK){
        LIBRADOR_LOG(LOG_WARNING, "iso transports are disabled on macOS (Tahoe kernel panic); using bulk\n");
        t = LABRADOR_TRANSPORT_BULK;
    }
#endif
    active_transport = t;
    return t;
}

int usbCallHandler::configure_transport_endpoints(){
    switch(active_transport){
    case LABRADOR_TRANSPORT_ISO6:
        num_data_endpoints = AIO_EP_ISO6_COUNT;
        data_packet_size = AIO_ISO6_PACKET_SIZE;
        for(int k = 0; k < AIO_EP_ISO6_COUNT; k++) pipeID[k] = AIO_EP_ISO6_FIRST + k;
        meta_pipeID = AIO_EP_ISO6_META;
        claimed_iface = AIO_IFACE_ISO6;
        break;
    case LABRADOR_TRANSPORT_ISO1:
        num_data_endpoints = 1;
        data_packet_size = ISO_PACKET_SIZE;
        pipeID[0] = AIO_EP_ISO1;
        meta_pipeID = AIO_EP_ISO1_META;
        claimed_iface = AIO_IFACE_ISO1;
        break;
    case LABRADOR_TRANSPORT_BULK:
        num_data_endpoints = 0;
        meta_pipeID = 0;
        claimed_iface = AIO_IFACE_BULK;
        break;
    default:
        return -1;
    }
    return 0;
}

// should only be called if iso_polling_thread is not active.  This means either the thread has never been set up OR it was previously set up but has exited iso_polling_function.
int usbCallHandler::setup_usb_iso(){
    LIBRADOR_LOG(LOG_DEBUG, "usbCallHandler::setup_usb_iso()\n");
    if(iso_polling_thread) {
        LIBRADOR_LOG(LOG_DEBUG, "iso polling thead already exists");
        return -1;
    }
    int error = start_streaming();
    if(error) {
        LIBRADOR_LOG(LOG_WARNING, "setup_usb_iso failed (%d)\n", error);
        return error;
    }
    LIBRADOR_LOG(LOG_DEBUG, "streaming started on transport %d", active_transport);
    iso_polling_thread = new std::thread(&usbCallHandler::iso_polling_function, this, ctx);
    iso_thread_active = true;
    return 0;
}

int usbCallHandler::total_inflight_transfers(){
    if(active_transport == LABRADOR_TRANSPORT_BULK) return NUM_BULK_CTX;
    return (num_data_endpoints + 1) * NUM_FUTURE_CTX;
}

int usbCallHandler::start_streaming(){
    resolve_transport();
    configure_transport_endpoints();
    reset_frame_state();
    iso_thread_shutdown_requested = false;
    iso_thread_shutdown_remaining_transfers = total_inflight_transfers();

    // Claim the transport's interface and select its streaming alt setting.
    // (Interface 0/alt 0 has no endpoints, so the initial claim during
    // connection is always safe - including on macOS Tahoe.)
    if(claimed_iface != 0 || !iface0_claimed){
        if(libusb_kernel_driver_active(handle, claimed_iface) == 1){
            libusb_detach_kernel_driver(handle, claimed_iface);
        }
        int error = libusb_claim_interface(handle, claimed_iface);
        if(error){
            LIBRADOR_LOG(LOG_WARNING, "claim_interface(%d) FAILED: %s\n", claimed_iface, libusb_error_name(error));
            return -3;
        }
        if(claimed_iface == 0){
            iface0_claimed = true;
        }
    }
    int error = libusb_set_interface_alt_setting(handle, claimed_iface, 1);
    if(error){
        LIBRADOR_LOG(LOG_WARNING, "set_interface_alt_setting(%d,1) FAILED: %s\n", claimed_iface, libusb_error_name(error));
        return -4;
    }

    alloc_iso_transfers();
    return submit_iso_transfers();
}

void usbCallHandler::alloc_iso_transfers(){
    alloc_transport = active_transport;
    if(active_transport == LABRADOR_TRANSPORT_BULK){
        for(int n=0;n<NUM_BULK_CTX;n++){
            bulkCtx[n] = libusb_alloc_transfer(0);
            libusb_fill_bulk_transfer(bulkCtx[n], handle, AIO_EP_BULK, bulkBuffer[n], BULK_CTX_SIZE, bulkCallback, this, 250);
        }
        return;
    }
    for(int n=0;n<NUM_FUTURE_CTX;n++){
        for (int k=0;k<num_data_endpoints;k++){
            isoCtx[k][n] = libusb_alloc_transfer(ISO_PACKETS_PER_CTX);
            libusb_fill_iso_transfer(isoCtx[k][n], handle, pipeID[k], dataBuffer[k][n], data_packet_size*ISO_PACKETS_PER_CTX, ISO_PACKETS_PER_CTX, isoCallback, this, 150); // assume the NUM_FUTURE_CTX=4 iso transfers are FIFO, in which case timeout should be > 1 ms (amount of data per packet) * NUM_PACKETS_PER_CTX * NUM_FUTURE_CTX
            libusb_set_iso_packet_lengths(isoCtx[k][n], data_packet_size);
        }
        metaCtx[n] = libusb_alloc_transfer(ISO_PACKETS_PER_CTX);
        libusb_fill_iso_transfer(metaCtx[n], handle, meta_pipeID, metaBuffer[n], AIO_META_PACKET_SIZE*ISO_PACKETS_PER_CTX, ISO_PACKETS_PER_CTX, metaIsoCallback, this, 150);
        libusb_set_iso_packet_lengths(metaCtx[n], AIO_META_PACKET_SIZE);
    }
}

int usbCallHandler::submit_iso_transfers(){
    if(active_transport == LABRADOR_TRANSPORT_BULK){
        for(int n=0;n<NUM_BULK_CTX;n++){
            int error = libusb_submit_transfer(bulkCtx[n]);
            if(error){
                LIBRADOR_LOG(LOG_ERROR, "bulk submit #%d FAILED with error %d %s\n", n, error, libusb_error_name(error));
                return error;
            }
        }
        return 0;
    }
    for(int n=0;n<NUM_FUTURE_CTX;n++){
        for (int k=0;k<num_data_endpoints;k++){
            int error = libusb_submit_transfer(isoCtx[k][n]);
            if(error){
                LIBRADOR_LOG(LOG_ERROR, "libusb_submit_transfer #%d:%d FAILED with error %d %s\n", n, k, error, libusb_error_name(error));
                return error;
            }
        }
        int error = libusb_submit_transfer(metaCtx[n]);
        if(error){
            LIBRADOR_LOG(LOG_ERROR, "meta submit #%d FAILED with error %d %s\n", n, error, libusb_error_name(error));
            return error;
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

    // EP0 can report a transient stall when a control request races the
    // streaming event thread (observed ~5% of the time on macOS with async
    // bulk transfers in flight; every occurrence recovers on an immediate
    // retry - the stall self-clears at the next SETUP).  All Labrador
    // vendor requests are idempotent, so retrying is safe.
    int error = 0;
    for(int attempt = 0; attempt < 3; attempt++){
        error = libusb_control_transfer(handle, RequestType, Request, Value, Index, controlBuffer, Length, 4000);
        if(error >= 0){
            return 0;
        }
        if(error != LIBUSB_ERROR_PIPE){
            break;
        }
        LIBRADOR_LOG(LOG_DEBUG, "control req 0x%02x stalled, retry %d\n", Request, attempt + 1);
    }
    LIBRADOR_LOG(LOG_WARNING, "send_control_transfer(req 0x%02x, val 0x%04x, idx 0x%04x, len %u) FAILED with error %s", Request, Value, Index, Length, libusb_error_name(error));
    connected = false;
    return error - 100;
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
            int i2 = buffer_for_daq->mostRecentAddressDAQ + (i - (numToGet-1)) * interval_samples;
            ix = i2 >= 0 ? i2 : i2 + buffer_for_daq->m_bufferLen;
            SDL_IOprintf(iostream, "%.0f ", buffer_for_daq->get_filtered_sample(ix, -1, 0, 0.0, false, true));
        }
    }
}

void usbCallHandler::drive_daq(int channel, int numToGet, int interval_samples, daqUnitOptions units_sel[2], const char * filepath) {
    LIBRADOR_LOG(LOG_DEBUG, "filepath: %s", filepath);
    SDL_IOStream* iostream = open_file(filepath);
    buffer_read_write_mutex.lock(); 
    if((channel == 1) || (channel == 3)) {
        if(deviceMode==6) {
            internal_o1_buffer_750->copy_to_daq();
        } else {
            internal_o1_buffer_375_CHA->copy_to_daq();
        }
    }
    if((channel == 2) || (channel == 3)) {
        internal_o1_buffer_375_CHB->copy_to_daq();
    }
    buffer_read_write_mutex.unlock(); 

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

    if(librador_get_host_hooks().daq_file_written)
        librador_get_host_hooks().daq_file_written(filepath);
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

// Oscilloscope calibration, ported from the Qt app's 3-stage wizard
// (Desktop_Interface/mainwindow.cpp on_actionCalibrate_triggered / calibrateStage2/3).
// vref is the measured reference-bias voltage exactly as the Qt app persists it
// (QSettings CalibrateVrefCHx, neutral 1.65); the value entering the conversion
// is 3.3 - vref, matching Qt's readSettingsFile (mainwindow.cpp:1510-1516).
// gain_scale multiplies the nominal R4/(R3+R4) = 75k/1075k frontend divider
// gain (neutral 1.0); Qt persists the absolute product as CalibrateGainCHx.
// Channel 1 owns both the 375 kSps CHA buffer and the 750 kSps buffer (Qt
// mirrors the CH1 values onto internalBuffer750); channel 2 owns the CHB
// buffer.  No USB traffic: safe to call while disconnected (e.g. restoring
// stored calibration at app startup).
int usbCallHandler::set_channel_calibration(int channel, double vref, double gain_scale){
    const double nominal_frontend_gain = 75.0/1075.0; // R4/(R3+R4), xmega.h
    double new_voltage_ref = 3.3 - vref;
    double new_frontend_gain = nominal_frontend_gain * gain_scale;

    buffer_read_write_mutex.lock();
    if(channel == 1){
        internal_o1_buffer_375_CHA->voltage_ref = new_voltage_ref;
        internal_o1_buffer_375_CHA->frontendGain = new_frontend_gain;
        internal_o1_buffer_750->voltage_ref = new_voltage_ref;
        internal_o1_buffer_750->frontendGain = new_frontend_gain;
    } else if(channel == 2){
        internal_o1_buffer_375_CHB->voltage_ref = new_voltage_ref;
        internal_o1_buffer_375_CHB->frontendGain = new_frontend_gain;
    } else {
        buffer_read_write_mutex.unlock();
        return -1; //Invalid channel
    }
    buffer_read_write_mutex.unlock();

    // ADC-unit trigger levels are derived from the conversion constants
    // (inverseSampleConvert); recompute them like setSettingsForChannel does.
    if(channel == 1){
        internal_o1_buffer_375_CHA->resetTrigger(current_scope_gain, deviceMode==7);
        internal_o1_buffer_750->resetTrigger(current_scope_gain, deviceMode==7);
    } else {
        internal_o1_buffer_375_CHB->resetTrigger(current_scope_gain, deviceMode==7);
    }
    return 0;
}

// PSU calibration offset (Qt genericusbdriver psu_offset / QSettings
// CalibratePsu).  Stored here and applied on the next set_psu_voltage call,
// exactly like Qt.  No USB traffic: safe to call while disconnected.
int usbCallHandler::set_psu_calibration_offset(double offset){
    psu_offset = offset;
    return 0;
}

#define CAL_PAGE_SIZE 32
#define CAL_MAGIC0 0xCA
#define CAL_MAGIC1 0x1B
#define CAL_VERSION 1

int usbCallHandler::save_calibration_to_device(double vref_ch1, double gain_scale_ch1,
        double vref_ch2, double gain_scale_ch2, double psu_offset_v){
    unsigned char page[CAL_PAGE_SIZE];
    float vals[5] = {(float)vref_ch1, (float)gain_scale_ch1,
                     (float)vref_ch2, (float)gain_scale_ch2, (float)psu_offset_v};
    memset(page, 0xFF, sizeof page);
    page[0] = CAL_MAGIC0;
    page[1] = CAL_MAGIC1;
    page[2] = CAL_VERSION;
    page[3] = 0;
    memcpy(&page[4], vals, sizeof vals);
    unsigned char cs = 0;
    for(int i = 0; i < 24; i++) cs ^= page[i];
    page[24] = cs;
    send_control_transfer_with_error_checks(0x40, 0xac, 0, 0, CAL_PAGE_SIZE, page);
    return 0;
}

int usbCallHandler::load_calibration_from_device(double *vref_ch1, double *gain_scale_ch1,
        double *vref_ch2, double *gain_scale_ch2, double *psu_offset_v){
    unsigned char page[CAL_PAGE_SIZE];
    memset(page, 0, sizeof page);
    send_control_transfer_with_error_checks(0xc0, 0xad, 0, 0, CAL_PAGE_SIZE, page);
    if(page[0] != CAL_MAGIC0 || page[1] != CAL_MAGIC1 || page[2] != CAL_VERSION){
        return 1;   // nothing (valid) stored - e.g. fresh EEPROM after a DFU erase
    }
    unsigned char cs = 0;
    for(int i = 0; i < 24; i++) cs ^= page[i];
    if(cs != page[24]){
        return 1;
    }
    float vals[5];
    memcpy(vals, &page[4], sizeof vals);
    if(vref_ch1) *vref_ch1 = vals[0];
    if(gain_scale_ch1) *gain_scale_ch1 = vals[1];
    if(vref_ch2) *vref_ch2 = vals[2];
    if(gain_scale_ch2) *gain_scale_ch2 = vals[3];
    if(psu_offset_v) *psu_offset_v = vals[4];
    return 0;
}

double usbCallHandler::get_scope_gain(){
    return current_scope_gain;
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
    // psu_offset mirrors the Qt app's PSU calibration
    // (Desktop_Interface/genericusbdriver.cpp setPsu: vinp = (voltage - psu_offset)/11).
    double vinp = (voltage - psu_offset)/11;
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

    char firmware_filename[64];
    snprintf(firmware_filename, sizeof firmware_filename, "labrafirm_%04X_%02x.hex",
        EXPECTED_FIRMWARE_VERSION, DEFINED_EXPECTED_VARIANT);

    if(!librador_get_host_hooks().prepare_firmware_hex){
        LIBRADOR_LOG(LOG_ERROR, "No prepare_firmware_hex host hook registered; cannot flash");
        return -1;
    }
    const char* firmware_copy_filepath
        = librador_get_host_hooks().prepare_firmware_hex(firmware_filename);
    if(!firmware_copy_filepath){
        LIBRADOR_LOG(LOG_ERROR, "prepare_firmware_hex failed for %s", firmware_filename);
        return -1;
    }

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
    snprintf(command, sizeof command, "dfu-programmer atxmega32a4u launch");
#ifdef PLATFORM_ANDROID
    libusb_device *device_ptr;
    exit_code = dfuprog_virtual_cmd(command, device_ptr, handle, ctx, 0);
    ctx = nullptr;
    handle=  nullptr;
#else
    // Desktop libdfuprog manages its own libusb context and device discovery
    exit_code = dfuprog_virtual_cmd(command);
#endif
    if (exit_code) {
        LIBRADOR_LOG(LOG_WARNING, "\n\n\n DFU LAUNCH ERROR\n\n\n");
        //return exit_code+300;
    }
}

int usbCallHandler::check_firmware_and_start_iso(){
    uint16_t firmver = get_firmware_version();
    LIBRADOR_LOG(LOG_DEBUG, "BOARD IS RUNNING FIRMWARE VERSION 0x%04hx", firmver);
    LIBRADOR_LOG(LOG_DEBUG, "EXPECTING FIRMWARE VERSION 0x%04hx", EXPECTED_FIRMWARE_VERSION);

    uint8_t variant = get_firmware_variant();
    LIBRADOR_LOG(LOG_DEBUG, "FIRMWARE VARIANT = 0x%02hx", variant);
    LIBRADOR_LOG(LOG_DEBUG, "EXPECTED VARIANT = 0x%02hx", DEFINED_EXPECTED_VARIANT);

    if((firmver != EXPECTED_FIRMWARE_VERSION) || (variant != DEFINED_EXPECTED_VARIANT)){
        LIBRADOR_LOG(LOG_DEBUG, "Unexpected Firmware!!");
        if(librador_get_host_hooks().request_firmware_flash)
            librador_get_host_hooks().request_firmware_flash();
        return 1;
    }
    return setup_usb_iso();
}

void usbCallHandler::teardown_connection(){
    // Idempotent and thread-safe: may be reached concurrently by the flash
    // worker and the main thread's disconnect detection (SIGABRT from a
    // double std::thread::join otherwise).
    std::lock_guard<std::mutex> teardown_lock(teardown_mutex);
    connected = false;
    if(data_frame_counter || frames_ok || frames_dropped){
        LIBRADOR_LOG(LOG_DEBUG, "frame stats at teardown: ok=%llu bad_csum=%llu dropped=%llu unvalidated=%llu (transport %d)\n",
            (unsigned long long)frames_ok.load(), (unsigned long long)frames_bad_checksum.load(),
            (unsigned long long)frames_dropped.load(), (unsigned long long)frames_unvalidated.load(),
            active_transport);
    }
    if(iso_polling_thread) {
        begin_iso_thread_shutdown();
        if(iso_polling_thread->joinable())
            iso_polling_thread->join();
        delete iso_polling_thread;

        // prepare for possible replug
        iso_thread_shutdown_requested = false;
        iso_polling_thread = nullptr;
        free_transfers();
    }
    if(handle){
        if(claimed_iface >= 0){
            // Deselect the streaming alt setting so the device stops
            // producing, then release.  Best effort - the device may
            // already be gone.
            libusb_set_interface_alt_setting(handle, claimed_iface, 0);
            if(claimed_iface != 0 || !iface0_claimed){
                libusb_release_interface(handle, claimed_iface);
            }
            claimed_iface = -1;
        }
        if(iface0_claimed){
            libusb_release_interface(handle, 0);
            iface0_claimed = false;
        }
        LIBRADOR_LOG(LOG_DEBUG, "Interface released\n");
        libusb_close(handle);
        handle = nullptr;
        LIBRADOR_LOG(LOG_DEBUG, "Device Closed\n");
    }
}

void usbCallHandler::free_transfers(){
    if(alloc_transport < 0) return;
    if(alloc_transport == LABRADOR_TRANSPORT_BULK){
        for(int n=0; n<NUM_BULK_CTX; n++){
            libusb_free_transfer(bulkCtx[n]);
        }
    } else {
        int ndata = (alloc_transport == LABRADOR_TRANSPORT_ISO6) ? AIO_EP_ISO6_COUNT : 1;
        for (int i=0; i<NUM_FUTURE_CTX; i++){
            for (int k=0; k<ndata; k++){
                libusb_free_transfer(isoCtx[k][i]);
            }
            libusb_free_transfer(metaCtx[i]);
        }
    }
    alloc_transport = -1;
    LIBRADOR_LOG(LOG_DEBUG, "Transfers freed.\n");
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
                dfu_launch();// firmware >= 0x000A boots after a single launch (bootloader-flag bug fixed)

                if(librador_get_host_hooks().confirm_firmware_flash)
                    librador_get_host_hooks().confirm_firmware_flash();
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
                check_firmware_and_start_iso();
                return;
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
        teardown_connection();
    }
}
#endif

SDL_IOStream* usbCallHandler::open_file(const char * filepath) {
    LIBRADOR_LOG(LOG_DEBUG, "daq filename: %s", filepath);
    if(librador_get_host_hooks().open_daq_stream)
        return librador_get_host_hooks().open_daq_stream(filepath);
    return SDL_IOFromFile(filepath, "w");
}

#ifndef PLATFORM_ANDROID
bool usbCallHandler::desktop_device_present(bool* bootloader_mode_out){
    // Legacy semantics: only the application-firmware and bootloader devices
    // count as present; a Gobindar-state (0xa000) board is NOT reported here
    // (callers of this signature predate Gobindar detection).
    bool app_mode = false;
    bool gobindar_mode = false;
    desktop_device_present_ex(&app_mode, bootloader_mode_out, &gobindar_mode);
    return app_mode || *bootloader_mode_out;
}

bool usbCallHandler::desktop_device_present_ex(bool* app_mode_out, bool* bootloader_mode_out, bool* gobindar_mode_out){
    *app_mode_out = false;
    *bootloader_mode_out = false;
    *gobindar_mode_out = false;
    if(init_libusb() < 0)
        return false;
    libusb_device** list;
    ssize_t n = libusb_get_device_list(ctx, &list);
    if(n < 0)
        return false;
    for(ssize_t i = 0; i < n; i++){
        libusb_device_descriptor desc;
        if(libusb_get_device_descriptor(list[i], &desc) != 0)
            continue;
        if(desc.idVendor != VID)
            continue;
        if(desc.idProduct == PID)
            *app_mode_out = true;
        else if(desc.idProduct == LABRADOR_BOOTLOADER_PID)
            *bootloader_mode_out = true;
        else if(desc.idProduct == LABRADOR_GOBINDAR_PID)
            *gobindar_mode_out = true;
    }
    libusb_free_device_list(list, 1);
    return *app_mode_out || *bootloader_mode_out || *gobindar_mode_out;
}

int usbCallHandler::desktop_connect(){
    if(connected)
        return 1;
    if(init_libusb() < 0)
        return -1;
    handle = libusb_open_device_with_vid_pid(ctx, VID, PID);
    if(!handle){
        LIBRADOR_LOG(LOG_WARNING, "Labrador device not found or could not be opened\n");
        return -2;
    }
    int error = claim_and_prepare();
    if(error)
        return error;
    connected = true;
    return check_firmware_and_start_iso();
}

void usbCallHandler::desktop_disconnect(){
    teardown_connection();
}

int usbCallHandler::desktop_flash_firmware(const char* hex_path){
    LIBRADOR_LOG(LOG_DEBUG, "desktop_flash_firmware: %s\n", hex_path);
    if(connected){
        set_bootloader_mode_allowed(true);
        reset_device(true);
        teardown_connection();
    }

    // Wait for the board to enumerate in bootloader mode
    bool bootloader = false;
    for(int attempt = 0; attempt < 50; attempt++){
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if(desktop_device_present(&bootloader) && bootloader)
            break;
    }
    if(!bootloader){
        LIBRADOR_LOG(LOG_ERROR, "Board never appeared in bootloader mode\n");
        return -1;
    }

    char command[512];
    int exit_code = dfuprog_virtual_cmd("dfu-programmer atxmega32a4u erase --force --debug 300");
    if(exit_code)
        LIBRADOR_LOG(LOG_WARNING, "ERROR ERASING FIRMWARE (%d)\n", exit_code);

    snprintf(command, sizeof command, "dfu-programmer atxmega32a4u flash %s --debug 300", hex_path);
    exit_code = dfuprog_virtual_cmd(command);
    if(exit_code){
        LIBRADOR_LOG(LOG_ERROR, "ERROR WRITING NEW FIRMWARE TO DEVICE (%d)\n", exit_code);
        return -2;
    }

    exit_code = dfuprog_virtual_cmd("dfu-programmer atxmega32a4u launch");
    if(exit_code)
        LIBRADOR_LOG(LOG_WARNING, "dfu launch reported %d (often benign)\n", exit_code);

    // The board may re-enumerate in bootloader mode once more (EEPROM flag);
    // keep launching until it comes up as the application firmware.
    for(int attempt = 0; attempt < 50; attempt++){
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if(!desktop_device_present(&bootloader))
            continue;
        if(!bootloader)
            break;
        dfuprog_virtual_cmd("dfu-programmer atxmega32a4u launch");
    }

    if(librador_get_host_hooks().confirm_firmware_flash)
        librador_get_host_hooks().confirm_firmware_flash();
    set_bootloader_mode_allowed(false);
    return desktop_connect();
}

// WARNING: currently unused — calling libusb_reset_device with live iso
// transfers crashed on macOS (2026-07-04). A working version must stop the
// iso thread and cancel transfers BEFORE the port reset. Kept as the anchor
// for that future fix; do not wire this up as-is.
int usbCallHandler::desktop_hard_reset(){
    if(!connected || !handle)
        return -1;
    LIBRADOR_LOG(LOG_DEBUG, "Hard USB reset (uninitialised-state auto-heal)\n");
    libusb_reset_device(handle);
    teardown_connection();
    return 0;
}

int usbCallHandler::desktop_bootloader_recover(const char* hex_path){
    LIBRADOR_LOG(LOG_DEBUG, "Bootloader-mode board found: attempting launch\n");
    dfu_launch();

    // Wait for the board to come back as the application firmware
    bool bootloader = false;
    bool present = false;
    for(int attempt = 0; attempt < 30; attempt++){
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        present = desktop_device_present(&bootloader);
        if(present)
            break;
    }
    if(present && !bootloader){
        int error = desktop_connect();
        if(error == 0 || error == 1){
            // 0 = streaming; 1 = connected but wrong firmware (the
            // request_firmware_flash hook has fired, the app takes it from here)
            LIBRADOR_LOG(LOG_DEBUG, "Bootloader recovery via launch succeeded\n");
            return error;
        }
    }

    // Launch didn't bring it back (application flash erased or corrupt):
    // do the full erase + flash + launch cycle.
    LIBRADOR_LOG(LOG_DEBUG, "Launch alone insufficient, reflashing\n");
    return desktop_flash_firmware(hex_path);
}

int usbCallHandler::desktop_gobindar_recover(const char* hex_path){
    LIBRADOR_LOG(LOG_DEBUG, "Gobindar-state board (PID 0x%04x): waiting for user to force bootloader\n", LABRADOR_GOBINDAR_PID);

    // Qt parity (genericusbdriver.cpp deGobindarise -> flashFirmware): the
    // 0xa000 device is never sent anything (Qt's bootloaderJump control
    // transfer is skipped because the driver is not "connected").  The user
    // shorts Digital Out 1 to GND and replugs the board, which forces the
    // hardware bootloader (0x2fe4); Qt then retries the dfu erase every
    // 200 ms until that device answers.  We wait for the bootloader to
    // enumerate at the same cadence, with a generous cap instead of Qt's
    // infinite loop, then reuse the standard erase + flash + launch cycle.
    set_bootloader_mode_allowed(true);

    bool app_mode = false;
    bool bootloader_mode = false;
    bool gobindar_mode = false;
    bool entered_bootloader = false;
    const int poll_interval_ms = 200;               // Qt's retry cadence
    const int max_attempts = 10 * 60 * 1000 / poll_interval_ms;  // ~10 minutes, user-paced
    for(int attempt = 0; attempt < max_attempts; attempt++){
        std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms));
        desktop_device_present_ex(&app_mode, &bootloader_mode, &gobindar_mode);
        if(bootloader_mode){
            entered_bootloader = true;
            break;
        }
    }
    if(!entered_bootloader){
        set_bootloader_mode_allowed(false);
        LIBRADOR_LOG(LOG_ERROR, "Gobindar recovery: board never entered bootloader mode\n");
        return -10;
    }

    // Bootloader is up: erase + flash + launch + double-launch, then connect.
    // (desktop_flash_firmware sees the bootloader immediately and resets
    // set_bootloader_mode_allowed(false) itself when done.)
    LIBRADOR_LOG(LOG_DEBUG, "Gobindar recovery: bootloader found, reflashing\n");
    return desktop_flash_firmware(hex_path);
}
#endif


