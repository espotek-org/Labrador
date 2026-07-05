//Include the ASF Licence!

#include <stdio.h>
#include <asf.h>
#include <string.h>

#include "ui.h"
#include "globals.h"
#include "tiny_adc.h"
#include "tiny_dma.h"
#include "tiny_timer.h"
#include "tiny_dac.h"
#include "tiny_uart.h"
#include "tiny_dig.h"
#include "tiny_calibration.h"
#include "tiny_eeprom.h"

volatile bool main_b_vendor_enable = false;

COMPILER_WORD_ALIGNED
volatile unsigned char isoBuf[BUFFER_SIZE];
COMPILER_WORD_ALIGNED
volatile unsigned char dacBuf_CH1[DACBUF_SIZE];
volatile unsigned char dacBuf_CH2[DACBUF_SIZE];

volatile unsigned char b1_state = 0;
volatile unsigned char b2_state = 0;
volatile unsigned char usb_state = 0;

volatile uint16_t dacBuf_len = 128;
volatile uint16_t auxDacBufLen = 128;
volatile unsigned char dummy = 0x55;
volatile unsigned char global_mode = 255;
volatile bool repeat_forever = true;

volatile char PSU_target = 0;

volatile unsigned char test_byte = 123;

uint32_t debug_counter;

unsigned char tripleUsbSuccess = 0;

volatile unsigned char firstFrame = 0;
volatile unsigned char tcinit = 0;

volatile unsigned int currentTrfcnt;
volatile unsigned char debugOnNextEnd = 0;

/*
#define CNT_CNT_MAX 256
volatile unsigned short cntCnt[CNT_CNT_MAX];
volatile unsigned short cntCntCnt = 0;
#define DEBUG_DIVISION 0
volatile unsigned char debug_divider = 0;
*/
volatile unsigned int median_TRFCNT = 65535;

volatile char debug_data[8] = "DEBUG123";

volatile unsigned short dma_ch0_ran;
volatile unsigned short dma_ch1_ran;

volatile unsigned char futureMode;
volatile unsigned char modeChanged = 0;

unified_debug uds;

const unsigned short firmver = FIRMWARE_VERSION_ID;

#ifdef AIO_INTERFACE
	const unsigned char variant = 0x03;
#elif defined(SINGLE_ENDPOINT_INTERFACE)
	const unsigned char variant = 0x02;
#else
	const unsigned char variant = 0x01;
#endif

#ifdef AIO_INTERFACE
volatile unsigned char active_transport = TRANSPORT_NONE;

//Frame validation, shared by all transports.  aio_seq advances every SOF
//while a transport streams, so frames the device had to skip appear as
//sequence gaps at the host.  The checksum is XOR over the payload the host
//receives that frame; if the ADC/DMA loop overwrites a buffer while it is
//still on the wire, the host sees a mismatch and can discard the frame.
//
//Bulk carries the header in-stream ahead of each payload (magic 0xEB 0x57).
//The iso interfaces carry it on a dedicated 8-byte iso meta endpoint (magic
//0xEB 0x58) with lag-1 semantics: the meta packet sent in frame N describes
//the payload of frame N-1, because N-1's payload is the last one whose bytes
//were stable for a full frame when the checksum was computed.
#define AIO_HDR_MAGIC0       0xEB
#define AIO_HDR_MAGIC1_BULK  0x57
#define AIO_HDR_MAGIC1_META  0x58
COMPILER_WORD_ALIGNED
static volatile unsigned char bulk_hdr[8];
COMPILER_WORD_ALIGNED
static volatile unsigned char iso_meta_buf[8];
static volatile unsigned char *bulk_payload_ptr;
static volatile unsigned short bulk_seg1_len;
static volatile unsigned short bulk_seg2_len;
static volatile unsigned short aio_seq = 0;
static volatile unsigned char bulk_busy = 0;
static volatile unsigned char meta_busy = 0;
static volatile unsigned short meta_prev_seq;
static volatile unsigned char meta_prev_csum0;
static volatile unsigned char meta_prev_csum1;
static volatile unsigned char meta_prev_mode;
static volatile unsigned char meta_prev_valid = 0;
volatile unsigned char aio_dbg[8];

static unsigned char aio_frame_csum(unsigned char state)
{
	//XOR of the payload the host would receive for double-buffer half
	//`state`:
	// - iso6 in modes <5 sends two half-buffers (one per analog channel)
	// - everything else sends one contiguous PACKET_SIZE half
	//The meta packets carry the checksum of BOTH halves; whichever half a
	//frame was armed from, the host can match it, and a frame overwritten
	//mid-flight by the ADC/DMA loop matches neither.
	unsigned short i;
	unsigned char c = 0;
	if((active_transport == TRANSPORT_ISO6) && (global_mode < 5)){
		volatile unsigned char *a = &isoBuf[state * HALFPACKET_SIZE];
		volatile unsigned char *b = &isoBuf[PACKET_SIZE + state * HALFPACKET_SIZE];
		for(i = 0; i < HALFPACKET_SIZE; i++){
			c ^= a[i];
			c ^= b[i];
		}
	}
	else{
		volatile unsigned char *p = &isoBuf[state * PACKET_SIZE];
		for(i = 0; i < PACKET_SIZE; i++){
			c ^= p[i];
		}
	}
	return c;
}
#endif

volatile unsigned char eeprom_buffer_write[EEPROM_PAGE_SIZE];
volatile unsigned char eeprom_buffer_read[EEPROM_PAGE_SIZE];

void jump_to_bootloader(){
	void(* start_bootloader)(void) = (void (*)(void))((BOOT_SECTION_START + ATMEL_DFU_OFFSET)>>1);
	EIND = BOOT_SECTION_START>>17;
	start_bootloader();
}

int main(void){	
	eeprom_safe_read();
	//Enter the bootloader only on the exact flag value written by vendor
	//request 0xa7.  A DFU chip erase leaves EEPROM at 0xff, which the old
	//truthiness check treated as "bootloader requested" - that is why every
	//firmware update used to need two launches (the first boot jumped
	//straight back into the bootloader while scrubbing the byte).
	if(1 == eeprom_buffer_read[0]){
			unsigned char clear_tries;
			memcpy(eeprom_buffer_write, eeprom_buffer_read, EEPROM_PAGE_SIZE);
			eeprom_buffer_write[0] = 0;
			//Verify the flag really cleared before entering the bootloader;
			//jumping with the write uncommitted leaves the flag set, so the
			//next app boot bounces straight back into the bootloader (the
			//old "have to launch twice" bug).
			for(clear_tries = 0; clear_tries < 3; clear_tries++){
				eeprom_safe_write();
				eeprom_safe_read();
				if(0 == eeprom_buffer_read[0]){
					break;
				}
			}
			jump_to_bootloader();
	}
	
	irq_initialize_vectors();
	cpu_irq_enable();
//	sysclk_init();	
	tiny_calibration_init();
		
	board_init();
	udc_start();
	tiny_dac_setup();
	tiny_dma_setup();
	tiny_adc_setup(0, 0);
	tiny_adc_pid_setup();
	tiny_adc_ch1setup(12);
	tiny_timer_setup();
	tiny_uart_setup();
	tiny_spi_setup();
	tiny_dig_setup();
			
	//USARTC0.DATA = 0x55;
	//asm("nop");
	


	strcpy(uds.header, "debug123");
	
	while (true) {
			asm("nop");
			asm("nop");
			asm("nop");
			asm("nop");
			asm("nop");
			asm("nop");
			asm("nop");
			asm("nop");
			asm("nop");
			asm("nop");
			asm("nop");
			asm("nop");
			asm("nop");
			asm("nop");
			if(modeChanged){
				switch(futureMode){
					case 0:
					tiny_dma_set_mode_0();
					break;
					case 1:
					tiny_dma_set_mode_1();
					break;
					case 2:
					tiny_dma_set_mode_2();
					break;
					case 3:
					tiny_dma_set_mode_3();
					break;
					case 4:
					tiny_dma_set_mode_4();
					break;
					case 5:
					tiny_dma_set_mode_5();
					break;
					case 6:
					tiny_dma_set_mode_6();
					break;
					case 7:
					tiny_dma_set_mode_7();
					break;
				}
				modeChanged = 0;
			}
	}
}

//CALLBACKS:
void main_suspend_action(void)
{
	return;
}

void main_resume_action(void)
{
	return;
}

#ifdef AIO_INTERFACE
static void main_aio_bulk_kick(void)
{
	//Queue the next bulk frame if the previous one has fully drained.
	unsigned short i;
	unsigned char csum;
	aio_seq++;
	if (bulk_busy) {
		return;
	}
	if (global_mode >= 5) {
		//Modes 6/7 run the DMA as a free-running circular buffer that is
		//not phase-locked to the USB frame, so the fixed halves tear
		//(measured ~50% checksum failures).  Bulk has no fixed framing:
		//send a rolling window of the PACKET_SIZE bytes the DMA just
		//finished writing, which is stable for the next full frame.
		unsigned short written = BUFFER_SIZE - DMA.CH0.TRFCNT;
		unsigned short start;
		if (written > BUFFER_SIZE) written = 0; //TRFCNT mid-reload
		start = (written >= PACKET_SIZE) ? (written - PACKET_SIZE)
		                                 : (written + PACKET_SIZE);
		bulk_payload_ptr = &isoBuf[start];
		bulk_seg1_len = BUFFER_SIZE - start;
		if (bulk_seg1_len >= PACKET_SIZE) {
			bulk_seg1_len = PACKET_SIZE;
			bulk_seg2_len = 0;
		} else {
			bulk_seg2_len = PACKET_SIZE - bulk_seg1_len;
		}
		csum = 0;
		for (i = 0; i < bulk_seg1_len; i++) csum ^= bulk_payload_ptr[i];
		for (i = 0; i < bulk_seg2_len; i++) csum ^= isoBuf[i];
	} else {
		//Modes 0-4 ping-pong the halves in lockstep with the SOF, so the
		//half the DMA is not writing this millisecond is stable.
		bulk_payload_ptr = &isoBuf[usb_state * PACKET_SIZE];
		bulk_seg1_len = PACKET_SIZE;
		bulk_seg2_len = 0;
		csum = aio_frame_csum(usb_state);
	}
	bulk_hdr[0] = AIO_HDR_MAGIC0;
	bulk_hdr[1] = AIO_HDR_MAGIC1_BULK;
	bulk_hdr[2] = aio_seq & 0xff;
	bulk_hdr[3] = (aio_seq >> 8) & 0xff;
	bulk_hdr[4] = PACKET_SIZE & 0xff;
	bulk_hdr[5] = (PACKET_SIZE >> 8) & 0xff;
	bulk_hdr[6] = csum;
	bulk_hdr[7] = global_mode;
	bulk_busy = 1;
	if (!udd_ep_run(UDI_AIO_EP_BULK_IN, false, (uint8_t *)bulk_hdr, sizeof(bulk_hdr), bulk_hdr_callback)) {
		bulk_busy = 0;
	}
}

static void main_aio_meta_fill_and_arm(udd_ep_id_t meta_ep)
{
	//Arm the meta endpoint with the header describing the frame currently
	//on the wire (latched at this frame's SOF); it transmits next frame,
	//giving the lag-1 semantics the host expects.  The buffer is only
	//written here, never while a transfer is pending.
	iso_meta_buf[0] = AIO_HDR_MAGIC0;
	iso_meta_buf[1] = AIO_HDR_MAGIC1_META;
	iso_meta_buf[2] = meta_prev_seq & 0xff;
	iso_meta_buf[3] = (meta_prev_seq >> 8) & 0xff;
	iso_meta_buf[4] = meta_prev_csum0;
	iso_meta_buf[5] = meta_prev_csum1;
	iso_meta_buf[6] = usb_state;
	iso_meta_buf[7] = meta_prev_mode;
	meta_busy = 1;
	if (!udd_ep_run(meta_ep, false, (uint8_t *)iso_meta_buf, sizeof(iso_meta_buf), meta_callback)) {
		meta_busy = 0;
		aio_dbg[1] |= 0x80;
	}
}

static void main_aio_meta_kick(udd_ep_id_t meta_ep)
{
	//Latch this frame's header; the meta endpoint re-arms itself from its
	//completion callback (once per frame).  Arming from here is only the
	//backstop for a broken chain (failed re-arm, first frame after enable).
	aio_seq++;
	meta_prev_seq = aio_seq;
	meta_prev_csum0 = aio_frame_csum(0);
	meta_prev_csum1 = aio_frame_csum(1);
	meta_prev_mode = global_mode;
	meta_prev_valid = 1;
	if (!meta_busy) {
		main_aio_meta_fill_and_arm(meta_ep);
	}
}

void bulk_hdr_callback(udd_ep_status_t status, iram_size_t nb_transfered, udd_ep_id_t ep)
{
	aio_dbg[4]++;
	if (status != UDD_EP_TRANSFER_OK) {
		bulk_busy = 0;
		return;
	}
	if (!udd_ep_run(UDI_AIO_EP_BULK_IN, false, (uint8_t *)bulk_payload_ptr, bulk_seg1_len, bulk_payload_callback)) {
		bulk_busy = 0;
	}
}

void bulk_payload_callback(udd_ep_status_t status, iram_size_t nb_transfered, udd_ep_id_t ep)
{
	aio_dbg[5]++;
	if (status != UDD_EP_TRANSFER_OK) {
		bulk_busy = 0;
		return;
	}
	if (bulk_seg2_len) {
		//Rolling window wrapped the end of isoBuf: send the tail segment.
		unsigned short len = bulk_seg2_len;
		bulk_seg2_len = 0;
		if (!udd_ep_run(UDI_AIO_EP_BULK_IN, false, (uint8_t *)&isoBuf[0], len, bulk_payload_callback)) {
			bulk_busy = 0;
		}
		return;
	}
	bulk_busy = 0;
}

void meta_callback(udd_ep_status_t status, iram_size_t nb_transfered, udd_ep_id_t ep)
{
	aio_dbg[3]++;
	meta_busy = 0;
	if (status != UDD_EP_TRANSFER_OK) {
		return;
	}
	//Immediately re-arm for the next frame with the currently latched
	//header.  Waiting for the next SOF instead would lose every other
	//frame when the host polls this endpoint late in the frame.
	if (meta_prev_valid) {
		main_aio_meta_fill_and_arm(ep);
	}
}
#endif

void main_sof_action(void)
{
	#ifdef SINGLE_ENDPOINT_INTERFACE
	switch(global_mode){
		case 0:
		tiny_dma_loop_mode_0();
		break;
		case 1:
		tiny_dma_loop_mode_1();
		break;
		case 2:
		tiny_dma_loop_mode_2();
		break;
		case 3:
		tiny_dma_loop_mode_3();
		break;
		case 4:
		tiny_dma_loop_mode_4();
		break;
		case 6:
		tiny_dma_loop_mode_6();
		break;
		case 7:
		tiny_dma_loop_mode_7();
		break;
		default:
		break;
	}
	#endif

	uds.trfcntL0 = DMA.CH0.TRFCNTL;
	uds.trfcntH0 = DMA.CH0.TRFCNTH;	
	uds.trfcntL1 = DMA.CH1.TRFCNTL;
	uds.trfcntH1 = DMA.CH1.TRFCNTH;
	uds.counterL = TC_CALI.CNTL;
	uds.counterH = TC_CALI.CNTH;
	if((DMA.CH0.TRFCNT > 325) && (DMA.CH0.TRFCNT < 425)){
		currentTrfcnt = DMA.CH0.TRFCNT;
		asm("nop");
	}
	if(firstFrame){
		tiny_calibration_first_sof();
		firstFrame = 0;
		tcinit = 1;
		return;
	}
	else{
		if(tcinit){
			if(calibration_values_found == 0x03){
				tiny_calibration_maintain();
				tiny_calibration_layer2();
			} else tiny_calibration_find_values();
			/*if(debug_divider == DEBUG_DIVISION){
				debug_divider = 0;
				cntCnt[cntCntCnt] = DMA.CH0.TRFCNT;
				if(cntCntCnt == (CNT_CNT_MAX - 1)){
					cntCntCnt = 0;
				}
				else cntCntCnt++;
			}
			else debug_divider++;*/
		}
	}
	
	if(debugOnNextEnd){
		currentTrfcnt = DMA.CH0.TRFCNT;
		debugOnNextEnd = 0;
	}
	#if defined(AIO_INTERFACE)
		if(active_transport == TRANSPORT_ISO6){
			if(global_mode < 5){
				usb_state = (DMA.CH0.TRFCNT < 375) ? 1 : 0;
			}
			else{
				usb_state = (DMA.CH0.TRFCNT < 750) ? 1 : 0;
			}
		}
		else{
			usb_state = !usb_state;
		}
		if(main_b_vendor_enable){
			switch(active_transport){
				case TRANSPORT_BULK:
					main_aio_bulk_kick();
					break;
				case TRANSPORT_ISO6:
					main_aio_meta_kick(UDI_AIO_EP_ISO6_META);
					break;
				case TRANSPORT_ISO1:
					main_aio_meta_kick(UDI_AIO_EP_ISO1_META);
					break;
			}
		}
	#elif !defined(SINGLE_ENDPOINT_INTERFACE)
		if(global_mode < 5){
			usb_state = (DMA.CH0.TRFCNT < 375) ? 1 : 0;
		}
		else{
			usb_state = (DMA.CH0.TRFCNT < 750) ? 1 : 0;
		}
	#else
		usb_state = !usb_state;
	#endif

	return;
}

bool main_vendor_enable(void)
{
#ifdef AIO_INTERFACE
	//Classic entry point (vendor request 0xaa): re-arm whatever transport
	//the host has selected via SET_INTERFACE.
	return main_aio_rearm();
#else
	main_b_vendor_enable = true;
	firstFrame = 1;
	udd_ep_run(0x81, false, (uint8_t *)&isoBuf[0], 125, iso_callback);
	#ifndef SINGLE_ENDPOINT_INTERFACE
	udd_ep_run(0x82, false, (uint8_t *)&isoBuf[125], 125, iso_callback);
	udd_ep_run(0x83, false, (uint8_t *)&isoBuf[250], 125, iso_callback);
	udd_ep_run(0x84, false, (uint8_t *)&isoBuf[375], 125, iso_callback);
	udd_ep_run(0x85, false, (uint8_t *)&isoBuf[500], 125, iso_callback);
	udd_ep_run(0x86, false, (uint8_t *)&isoBuf[625], 125, iso_callback);
	#endif
	return true;
#endif
}

void main_vendor_disable(void)
{
	main_b_vendor_enable = false;
}

#ifdef AIO_INTERFACE
static void main_aio_arm_endpoints(void)
{
	unsigned char i;
	aio_seq = 0;
	meta_prev_valid = 0;
	meta_busy = 0;
	aio_dbg[1] = 0;
	aio_dbg[2] = 0;
	aio_dbg[3] = 0;
	aio_dbg[4] = 0;
	aio_dbg[5] = 0;
	switch(active_transport){
		case TRANSPORT_ISO6:
			for(i = 0; i < 6; i++){
				udd_ep_abort(0x81 + i); //clear any stale job
				if(!udd_ep_run(0x81 + i, false, (uint8_t *)&isoBuf[i * 125], 125, iso_callback)){
					aio_dbg[1] |= (1 << i);
				}
			}
			udd_ep_abort(UDI_AIO_EP_ISO6_META);
			break;
		case TRANSPORT_ISO1:
			udd_ep_abort(UDI_AIO_EP_ISO1_IN);
			if(!udd_ep_run(UDI_AIO_EP_ISO1_IN, false, (uint8_t *)&isoBuf[0], PACKET_SIZE, iso_callback)){
				aio_dbg[1] |= 0x01;
			}
			udd_ep_abort(UDI_AIO_EP_ISO1_META);
			break;
		case TRANSPORT_BULK:
			//Frames are queued from the SOF handler once the pipe is idle.
			udd_ep_abort(UDI_AIO_EP_BULK_IN);
			bulk_busy = 0;
			break;
	}
}

bool main_aio_iface_enable(uint8_t iface)
{
	switch(iface){
		case UDI_AIO_IFACE_ISO6: active_transport = TRANSPORT_ISO6; break;
		case UDI_AIO_IFACE_ISO1: active_transport = TRANSPORT_ISO1; break;
		case UDI_AIO_IFACE_BULK: active_transport = TRANSPORT_BULK; break;
		default: return false;
	}
	//The DMA regime (repeat vs. single-shot, block length, priority) depends
	//on the transport, so rebuild the current acquisition mode under it.
	tiny_dma_apply_transport();
	main_b_vendor_enable = true;
	firstFrame = 1;
	main_aio_arm_endpoints();
	return true;
}

void main_aio_iface_disable(uint8_t iface)
{
	//Ignore stale disables from an interface that is not the active one
	//(the host may bounce alternate settings while switching transports).
	switch(iface){
		case UDI_AIO_IFACE_ISO6: if(active_transport != TRANSPORT_ISO6) return; break;
		case UDI_AIO_IFACE_ISO1: if(active_transport != TRANSPORT_ISO1) return; break;
		case UDI_AIO_IFACE_BULK: if(active_transport != TRANSPORT_BULK) return; break;
		default: return;
	}
	active_transport = TRANSPORT_NONE;
	main_b_vendor_enable = false;
	bulk_busy = 0;
}

bool main_aio_rearm(void)
{
	if(active_transport == TRANSPORT_NONE){
		return false;
	}
	firstFrame = 1;
	main_aio_arm_endpoints();
	return true;
}
#endif

bool main_setup_out_received(void)
{
	return 1;
}

bool main_setup_in_received(void)
{
	return true;
}

void iso_callback(udd_ep_status_t status, iram_size_t nb_transfered, udd_ep_id_t ep){
	#if defined(AIO_INTERFACE)
		aio_dbg[2]++;
		if (status != UDD_EP_TRANSFER_OK) {
			//Aborted (endpoint freed on an alt-setting change): do NOT
			//re-arm, or the job is left busy forever and the next
			//SET_INTERFACE cannot start the stream.
			return;
		}
		if(active_transport == TRANSPORT_ISO6){
			unsigned short offset = (ep - 0x81) * 125;
			if (global_mode < 5){
				if(ep > 0x83) offset += 375; //Shift from range [375, 750]  to [750, 1125]  Don't do this in modes 6 and 7 because they use 750 byte long sub-buffers.
				udd_ep_run(ep, false, (uint8_t *)&isoBuf[usb_state * HALFPACKET_SIZE + offset], 125, iso_callback);
			}
			else{
				udd_ep_run(ep, false, (uint8_t *)&isoBuf[usb_state * PACKET_SIZE + offset], 125, iso_callback);
			}
		}
		else if(active_transport == TRANSPORT_ISO1){
			udd_ep_run(UDI_AIO_EP_ISO1_IN, false, (uint8_t *)&isoBuf[usb_state * PACKET_SIZE], PACKET_SIZE, iso_callback);
		}
		return;
	#elif !defined(SINGLE_ENDPOINT_INTERFACE)
		unsigned short offset = (ep - 0x81) * 125;
		if (global_mode < 5){
			if(ep > 0x83) offset += 375; //Shift from range [375, 750]  to [750, 1125]  Don't do this in modes 6 and 7 because they use 750 byte long sub-buffers.
			udd_ep_run(ep, false, (uint8_t *)&isoBuf[usb_state * HALFPACKET_SIZE + offset], 125, iso_callback);
		}
		else{
			udd_ep_run(ep, false, (uint8_t *)&isoBuf[usb_state * PACKET_SIZE + offset], 125, iso_callback);
		}
		return;
	#else
		udd_ep_run(0x81, false, (uint8_t *)&isoBuf[usb_state * PACKET_SIZE], PACKET_SIZE, iso_callback);
	#endif
}
