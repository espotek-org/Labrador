/*
 * globals.h
 *
 * Created: 18/04/2015 12:44:42 PM
 *  Author: Esposch
 */ 


#ifndef GLOBALS_H_
#define GLOBALS_H_

//#define SINGLE_ENDPOINT_INTERFACE
//#define AIO_INTERFACE

#ifdef AIO_INTERFACE
	#ifdef SINGLE_ENDPOINT_INTERFACE
		#error "AIO_INTERFACE and SINGLE_ENDPOINT_INTERFACE are mutually exclusive"
	#endif
	//Transport selected by the host via SET_INTERFACE (alt setting 1 on one of the three interfaces)
	#define TRANSPORT_NONE 0
	#define TRANSPORT_ISO6 1
	#define TRANSPORT_ISO1 2
	#define TRANSPORT_BULK 3
	#define TRANSPORT_INT  4
#endif

//#define VERO
#define OVERCLOCK 48
//#define FIRMWARE_VERSION_ID 0x0007
#define ATMEL_DFU_OFFSET 0x01fc

#define TC_SPISLAVE TCD0
#define TC_PSU TCD1
#define TC_PSU_OVF TCD1_OVF_vect
#define TC_DAC TCC0
#define TCDAC_OVF EVSYS_CHMUX_TCC0_OVF_gc
#define TC_AUXDAC TCC1
#define TC_CALI TCE0
#define TCDAC_AUX_OVF EVSYS_CHMUX_TCC1_OVF_gc
#define HALFPACKET_SIZE 375
#define PACKET_SIZE 750
#define B2_START 1125
#define BUFFER_SIZE (PACKET_SIZE*2)
#define DACBUF_SIZE 512

//Bulk transfers are padded to 64-byte multiples so the stream never
//contains a short packet (a short packet terminates the host's URB early,
//collapsing its read-ahead queue to nothing and blowing the 1 ms drain
//deadline).  The pad tail lets a 768-byte padded read of the second half
//stay inside the array.
#define ISOBUF_BULK_PAD 20
COMPILER_WORD_ALIGNED
extern volatile unsigned char isoBuf[BUFFER_SIZE + ISOBUF_BULK_PAD];
COMPILER_WORD_ALIGNED
extern volatile unsigned char dacBuf_CH1[DACBUF_SIZE];
extern volatile unsigned char dacBuf_CH2[DACBUF_SIZE];

extern volatile unsigned char b1_state;
extern volatile unsigned char b2_state;
extern volatile unsigned char usb_state;

extern volatile bool main_b_vendor_enable;

extern volatile uint16_t dacBuf_len;
extern volatile uint16_t auxDacBufLen;

extern volatile unsigned char dummy;

extern volatile unsigned char global_mode;
extern volatile bool repeat_forever;

extern volatile char PSU_target;

extern volatile unsigned char test_byte;

extern volatile unsigned char debugOnNextEnd;

extern volatile unsigned int median_TRFCNT;

extern volatile unsigned short dma_ch0_ran;
extern volatile unsigned short dma_ch1_ran;

extern volatile unsigned char futureMode;
extern volatile unsigned char modeChanged;

#ifdef AIO_INTERFACE
extern volatile unsigned char active_transport;
//Debug state readable via vendor request 0xab:
//[0]=active_transport [1]=endpoint arm-failure mask [2]=iso_callback count
//[3]=meta_callback count [4]=bulk hdr cb count [5]=bulk payload cb count
//[6]=usb_state [7]=global_mode
extern volatile unsigned char aio_dbg[8];
#endif

COMPILER_WORD_ALIGNED
extern const unsigned short firmver;
extern const unsigned char variant;

#include "unified_debug_structure.h"
extern unified_debug uds;

#endif /* GLOBALS_H_ */
