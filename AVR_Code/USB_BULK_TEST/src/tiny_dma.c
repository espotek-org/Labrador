/*
 * tiny_dma.c
 *
 * Created: 25/06/2015 9:00:42 AM
 *  Author: Esposch
 */ 

#include "tiny_dma.h"
#include "tiny_adc.h"
#include "tiny_uart.h"
#include "tiny_calibration.h"
#include "globals.h"
#include "util/delay.h"

#ifndef SINGLE_ENDPOINT_INTERFACE
	#define DMA_STANDARD_INTERRUPT (0x00)
	#define DMA_STANDARD_CTRLA (DMA_CH_BURSTLEN_1BYTE_gc | DMA_CH_SINGLE_bm | DMA_CH_REPEAT_bm)
	#define DMA_STANDARD_TRANSFER_LENGTH (PACKET_SIZE)
#else
	#define DMA_STANDARD_INTERRUPT (0x03)
	#define DMA_STANDARD_CTRLA (DMA_CH_BURSTLEN_1BYTE_gc | DMA_CH_SINGLE_bm)
	#define DMA_STANDARD_TRANSFER_LENGTH (HALFPACKET_SIZE)
#endif

void tiny_dma_setup(void){
	//Turn on DMA
	PR.PRGEN &=0b111111110; //Turn on DMA clk
	#ifndef SINGLE_ENDPOINT_INTERFACE
		DMA.CTRL = DMA_ENABLE_bm | DMA_PRIMODE_CH0123_gc;
	#else 
		DMA.CTRL = DMA_ENABLE_bm | DMA_PRIMODE_RR0123_gc;
		#warning "Round Robin on DMA"
	#endif
	
}
void tiny_dma_flush(void){
	DMA.CH0.CTRLA = 0x00;
	DMA.CH0.CTRLA = DMA_CH_RESET_bm;

	DMA.CH1.CTRLA = 0x00;
	DMA.CH1.CTRLA = DMA_CH_RESET_bm;
	
	DMA.CH2.CTRLA = 0x00;
	DMA.CH2.CTRLA = DMA_CH_RESET_bm;
	
	DMA.CH3.CTRLA = 0x00;
	DMA.CH3.CTRLA = DMA_CH_RESET_bm;
	
	b1_state = 0;
	b2_state = 0;
	usb_state = 0;
	
	dma_ch0_ran = 0;
	dma_ch1_ran = 0;
}
void tiny_dma_delayed_set(unsigned char mode){
	futureMode = mode;
	modeChanged = 1;	
}
void tiny_dma_set_mode_0(void){
	
	global_mode = 0;
	
	tiny_dma_flush();
	
	// TX UART waveform
	if(!repeat_forever)
	{
		DMA.CH2.REPCNT = 1; //Do not repeat
		DMA.CH2.CTRLA = DMA_CH_BURSTLEN_1BYTE_gc | DMA_CH_SINGLE_bm;
	}
	// Remaining waveforms
	else
	{
		DMA.CH2.REPCNT = 0; //Repeat forever!
		DMA.CH2.CTRLA = DMA_CH_BURSTLEN_1BYTE_gc | DMA_CH_SINGLE_bm | DMA_CH_REPEAT_bm;
	}
	DMA.CH2.CTRLB = 0x00; //No interrupt for DacBuf!!
	DMA.CH2.ADDRCTRL = DMA_CH_DESTRELOAD_BURST_gc | DMA_CH_DESTDIR_INC_gc | DMA_CH_SRCRELOAD_BLOCK_gc | DMA_CH_SRCDIR_INC_gc;   //Dest reloads after each burst, with byte incrementing.  Src reloads at end of block, also incrementing address.
	DMA.CH2.TRIGSRC = DMA_CH_TRIGSRC_EVSYS_CH1_gc;	//Triggered from TCC0 when it hits PER
	DMA.CH2.TRFCNT = auxDacBufLen;

	DMA.CH2.SRCADDR0 = (( (uint16_t) &dacBuf_CH2[0]) >> 0) & 0xFF; //Source address is dacbuf
	DMA.CH2.SRCADDR1 = (( (uint16_t) &dacBuf_CH2[0]) >> 8) & 0xFF;
	DMA.CH2.SRCADDR2 = 0x00;
	
	DMA.CH2.DESTADDR0 = (( (uint16_t) &DACB.CH1DATAH) >> 0) & 0xFF;  //Dest address is high byte of DAC register
	DMA.CH2.DESTADDR1 = (( (uint16_t) &DACB.CH1DATAH) >> 8) & 0xFF;
	DMA.CH2.DESTADDR2 = 0x00;
	
	//Must enable last for REPCNT won't work!
	DMA.CH2.CTRLA |= DMA_CH_ENABLE_bm;  //Enable!
		
	DMA.CH3.REPCNT = 0; //Repeat forever!
	DMA.CH3.CTRLA = DMA_CH_BURSTLEN_1BYTE_gc | DMA_CH_SINGLE_bm | DMA_CH_REPEAT_bm;
	DMA.CH3.CTRLB = 0x00; //Hi interrupt on block complete
	DMA.CH3.ADDRCTRL = DMA_CH_DESTRELOAD_BURST_gc | DMA_CH_DESTDIR_INC_gc | DMA_CH_SRCRELOAD_BLOCK_gc | DMA_CH_SRCDIR_INC_gc;   //Dest reloads after each burst, with byte incrementing.  Src reloads at end of block, also incrementing address.
	DMA.CH3.TRIGSRC = DMA_CH_TRIGSRC_EVSYS_CH2_gc;	//Triggered from TCC0 when it hits PER
	DMA.CH3.TRFCNT = dacBuf_len;

	DMA.CH3.SRCADDR0 = (( (uint16_t) &dacBuf_CH1[0]) >> 0) & 0xFF; //Source address is dacbuf
	DMA.CH3.SRCADDR1 = (( (uint16_t) &dacBuf_CH1[0]) >> 8) & 0xFF;
	DMA.CH3.SRCADDR2 = 0x00;
	
	DMA.CH3.DESTADDR0 = (( (uint16_t) &DACB.CH0DATAH) >> 0) & 0xFF;  //Dest address is high byte of DAC register
	DMA.CH3.DESTADDR1 = (( (uint16_t) &DACB.CH0DATAH) >> 8) & 0xFF;
	DMA.CH3.DESTADDR2 = 0x00;
	
	//Must enable last for REPCNT won't work!
	DMA.CH3.CTRLA |= DMA_CH_ENABLE_bm;  //Enable!	
	
	DMA.CH0.CTRLA = 0x00;
	DMA.CH0.CTRLA = DMA_CH_RESET_bm;
		
	DMA.CH0.CTRLA = DMA_STANDARD_CTRLA;
	DMA.CH0.CTRLB = DMA_STANDARD_INTERRUPT;
	DMA.CH0.ADDRCTRL = DMA_CH_SRCDIR_FIXED_gc | DMA_CH_DESTDIR_INC_gc | DMA_CH_DESTRELOAD_BLOCK_gc;   //Source reloads after each burst, with byte incrementing.  Dest does not reload, but does increment address.
	DMA.CH0.TRIGSRC = DMA_CH_TRIGSRC_ADCA_CH0_gc;	//Triggered from ADCA channel 0
	DMA.CH0.TRFCNT = DMA_STANDARD_TRANSFER_LENGTH;
		
	DMA.CH0.SRCADDR0 = (( (uint16_t) &ADCA.CH0.RESL) >> 0) & 0xFF; //Source address is ADC
	DMA.CH0.SRCADDR1 = (( (uint16_t) &ADCA.CH0.RESL) >> 8) & 0xFF;
	DMA.CH0.SRCADDR2 = 0x00;
		
	DMA.CH0.DESTADDR0 = (( (uint16_t) &isoBuf[0]) >> 0) & 0xFF;  //Dest address is isoBuf
	DMA.CH0.DESTADDR1 = (( (uint16_t) &isoBuf[0]) >> 8) & 0xFF;
	DMA.CH0.DESTADDR2 = 0x00;
		
	tiny_calibration_synchronise_phase(500, 200);
	median_TRFCNT = 200;
	median_TRFCNT_delay = 1; //Wait a few frames before actually setting median_TRFCNT, in case a SOF interrupt was queued during tiny_dma_set_mode_xxx.
	DMA.CH0.CTRLA |= DMA_CH_ENABLE_bm;  //Enable!
	
}

void tiny_dma_loop_mode_0(void){
	return;
}

void tiny_dma_set_mode_1(void){
	global_mode = 1;
	
	tiny_dma_flush();
	
	//AUX channel (to keep it tx, therefore always rx)
	DMA.CH2.CTRLA = 0x00;
	DMA.CH2.CTRLA = DMA_CH_RESET_bm;
		
	DMA.CH2.CTRLA = DMA_CH_BURSTLEN_1BYTE_gc | DMA_CH_SINGLE_bm; //Do not repeat!
	DMA.CH2.CTRLB = 0x00;  //No int
	DMA.CH2.ADDRCTRL = DMA_CH_SRCDIR_FIXED_gc | DMA_CH_DESTDIR_FIXED_gc;   //Source and address fixed.
	DMA.CH2.TRIGSRC = DMA_CH_TRIGSRC_USARTC0_RXC_gc;
	DMA.CH2.TRFCNT = 0;
	DMA.CH2.REPCNT = 0;
		
	DMA.CH2.SRCADDR0 = (( (uint16_t) &dummy) >> 0) & 0xFF;
	DMA.CH2.SRCADDR1 = (( (uint16_t) &dummy) >> 8) & 0xFF;
	DMA.CH2.SRCADDR2 = 0x00;
		
	DMA.CH2.DESTADDR0 = (( (uint16_t) &USARTC0.DATA) >> 0) & 0xFF;
	DMA.CH2.DESTADDR1 = (( (uint16_t) &USARTC0.DATA) >> 8) & 0xFF;
	DMA.CH2.DESTADDR2 = 0x00;
		
	//Must enable last for REPCNT won't work!
	DMA.CH2.CTRLA |= DMA_CH_REPEAT_bm | DMA_CH_ENABLE_bm;  //Enable!

	USARTC0.DATA = 0x55;
	USARTC0.DATA = 0x55;

	// TX UART waveform
	if(!repeat_forever)
	{
		DMA.CH3.REPCNT = 1; //Do not repeat
		DMA.CH3.CTRLA = DMA_CH_BURSTLEN_1BYTE_gc | DMA_CH_SINGLE_bm;
	}
	// Remaining waveforms
	else
	{
		DMA.CH3.REPCNT = 0; //Repeat forever!
		DMA.CH3.CTRLA = DMA_CH_BURSTLEN_1BYTE_gc | DMA_CH_SINGLE_bm | DMA_CH_REPEAT_bm;
	}
	DMA.CH3.CTRLB = 0x00; //No interrupt for DacBuf!!
	DMA.CH3.ADDRCTRL = DMA_CH_DESTRELOAD_BURST_gc | DMA_CH_DESTDIR_INC_gc | DMA_CH_SRCRELOAD_BLOCK_gc | DMA_CH_SRCDIR_INC_gc;   //Dest reloads after each burst, with byte incrementing.  Src reloads at end of block, also incrementing address.
	DMA.CH3.TRIGSRC = DMA_CH_TRIGSRC_EVSYS_CH1_gc;	//Triggered from TCC0 when it hits PER
	DMA.CH3.TRFCNT = auxDacBufLen;

	DMA.CH3.SRCADDR0 = (( (uint16_t) &dacBuf_CH2[0]) >> 0) & 0xFF; //Source address is dacbuf
	DMA.CH3.SRCADDR1 = (( (uint16_t) &dacBuf_CH2[0]) >> 8) & 0xFF;
	DMA.CH3.SRCADDR2 = 0x00;
	
	DMA.CH3.DESTADDR0 = (( (uint16_t) &DACB.CH1DATAH) >> 0) & 0xFF;  //Dest address is high byte of DAC register
	DMA.CH3.DESTADDR1 = (( (uint16_t) &DACB.CH1DATAH) >> 8) & 0xFF;
	DMA.CH3.DESTADDR2 = 0x00;
	
	//Must enable last for REPCNT won't work!
	DMA.CH3.CTRLA |= DMA_CH_ENABLE_bm;  //Enable!
	
	DMA.CH1.CTRLA = DMA_STANDARD_CTRLA;
	DMA.CH1.CTRLB = DMA_STANDARD_INTERRUPT;
	DMA.CH1.ADDRCTRL = DMA_CH_SRCRELOAD_BURST_gc | DMA_CH_SRCDIR_INC_gc | DMA_CH_DESTDIR_INC_gc | DMA_CH_DESTRELOAD_BLOCK_gc;   //Source reloads after each burst, with byte incrementing.  Dest does not reload, but does increment address.
	DMA.CH1.TRIGSRC = DMA_CH_TRIGSRC_USARTC0_RXC_gc;
	DMA.CH1.TRFCNT = DMA_STANDARD_TRANSFER_LENGTH;
		
	DMA.CH1.SRCADDR0 = (( (uint16_t) &USARTC0.DATA) >> 0) & 0xFF;
	DMA.CH1.SRCADDR1 = (( (uint16_t) &USARTC0.DATA) >> 8) & 0xFF;
	DMA.CH1.SRCADDR2 = 0x00;
		
	DMA.CH1.DESTADDR0 = (( (uint16_t) &isoBuf[PACKET_SIZE]) >> 0) & 0xFF;  //Dest address is isoBuf
	DMA.CH1.DESTADDR1 = (( (uint16_t) &isoBuf[PACKET_SIZE]) >> 8) & 0xFF;
	DMA.CH1.DESTADDR2 = 0x00;
	
	DMA.CH0.CTRLA = DMA_STANDARD_CTRLA;
	DMA.CH0.CTRLB = DMA_STANDARD_INTERRUPT;
	DMA.CH0.ADDRCTRL = DMA_CH_SRCDIR_FIXED_gc | DMA_CH_DESTDIR_INC_gc | DMA_CH_DESTRELOAD_BLOCK_gc;   //Source reloads after each burst, with byte incrementing.  Dest does not reload, but does increment address.
	DMA.CH0.TRIGSRC = DMA_CH_TRIGSRC_ADCA_CH0_gc;	//Triggered from ADCA channel 0
	DMA.CH0.TRFCNT = DMA_STANDARD_TRANSFER_LENGTH;
	
	DMA.CH0.SRCADDR0 = (( (uint16_t) &ADCA.CH0.RESL) >> 0) & 0xFF; //Source address is ADC
	DMA.CH0.SRCADDR1 = (( (uint16_t) &ADCA.CH0.RESL) >> 8) & 0xFF;
	DMA.CH0.SRCADDR2 = 0x00;
	
	DMA.CH0.DESTADDR0 = (( (uint16_t) &isoBuf[0]) >> 0) & 0xFF;  //Dest address is isoBuf
	DMA.CH0.DESTADDR1 = (( (uint16_t) &isoBuf[0]) >> 8) & 0xFF;
	DMA.CH0.DESTADDR2 = 0x00;
	
	tiny_calibration_synchronise_phase(500, 200);
	median_TRFCNT = 200;
	median_TRFCNT_delay = 1; //Wait a few frames before actually setting median_TRFCNT, in case a SOF interrupt was queued during tiny_dma_set_mode_xxx.
	DMA.CH1.CTRLA |= DMA_CH_ENABLE_bm;  //Enable!
	DMA.CH0.CTRLA |= DMA_CH_ENABLE_bm;  //Enable!	
}

void tiny_dma_loop_mode_1(void){
	return;
}

void tiny_dma_set_mode_2(void){
	
	global_mode = 2;
	
	tiny_dma_flush();
	
	// TX UART waveform
	if(!repeat_forever)
	{
		DMA.CH2.REPCNT = 1; //Do not repeat
		DMA.CH2.CTRLA = DMA_CH_BURSTLEN_1BYTE_gc | DMA_CH_SINGLE_bm;
	}
	// Remaining waveforms
	else
	{
		DMA.CH2.REPCNT = 0; //Repeat forever!
		DMA.CH2.CTRLA = DMA_CH_BURSTLEN_1BYTE_gc | DMA_CH_SINGLE_bm | DMA_CH_REPEAT_bm;
	}
	DMA.CH2.CTRLB = 0x00; //No interrupt for DacBuf!!
	DMA.CH2.ADDRCTRL = DMA_CH_DESTDIR_FIXED_gc | DMA_CH_SRCRELOAD_BLOCK_gc | DMA_CH_SRCDIR_INC_gc;   //Dest reloads after each burst, with byte incrementing.  Src reloads at end of block, also incrementing address.
	DMA.CH2.TRIGSRC = DMA_CH_TRIGSRC_EVSYS_CH1_gc;	//Triggered from TCC0 when it hits PER
	DMA.CH2.TRFCNT = auxDacBufLen;

	DMA.CH2.SRCADDR0 = (( (uint16_t) &dacBuf_CH2[0]) >> 0) & 0xFF; //Source address is dacbuf
	DMA.CH2.SRCADDR1 = (( (uint16_t) &dacBuf_CH2[0]) >> 8) & 0xFF;
	DMA.CH2.SRCADDR2 = 0x00;
	
	DMA.CH2.DESTADDR0 = (( (uint16_t) &DACB.CH1DATAH) >> 0) & 0xFF;  //Dest address is high byte of DAC register
	DMA.CH2.DESTADDR1 = (( (uint16_t) &DACB.CH1DATAH) >> 8) & 0xFF;
	DMA.CH2.DESTADDR2 = 0x00;
	
	//Must enable last for REPCNT won't work!
	DMA.CH2.CTRLA |= DMA_CH_ENABLE_bm;  //Enable!
	
	DMA.CH3.REPCNT = 0; //Repeat forever!
	DMA.CH3.CTRLA = DMA_CH_BURSTLEN_1BYTE_gc | DMA_CH_SINGLE_bm | DMA_CH_REPEAT_bm;
	DMA.CH3.CTRLB = 0x00; //Hi interrupt on block complete
	DMA.CH3.ADDRCTRL = DMA_CH_DESTDIR_FIXED_gc | DMA_CH_SRCRELOAD_BLOCK_gc | DMA_CH_SRCDIR_INC_gc;   //Dest reloads after each burst, with byte incrementing.  Src reloads at end of block, also incrementing address.
	DMA.CH3.TRIGSRC = DMA_CH_TRIGSRC_EVSYS_CH2_gc;	//Triggered from TCC0 when it hits PER
	DMA.CH3.TRFCNT = dacBuf_len;

	DMA.CH3.SRCADDR0 = (( (uint16_t) &dacBuf_CH1[0]) >> 0) & 0xFF; //Source address is dacbuf
	DMA.CH3.SRCADDR1 = (( (uint16_t) &dacBuf_CH1[0]) >> 8) & 0xFF;
	DMA.CH3.SRCADDR2 = 0x00;
	
	DMA.CH3.DESTADDR0 = (( (uint16_t) &DACB.CH0DATAH) >> 0) & 0xFF;  //Dest address is high byte of DAC register
	DMA.CH3.DESTADDR1 = (( (uint16_t) &DACB.CH0DATAH) >> 8) & 0xFF;
	DMA.CH3.DESTADDR2 = 0x00;
	
	//Must enable last for REPCNT won't work!
	DMA.CH3.CTRLA |= DMA_CH_ENABLE_bm;  //Enable!
	
	DMA.CH0.CTRLA = 0x00;
	DMA.CH0.CTRLA = DMA_CH_RESET_bm;
	
	DMA.CH0.CTRLA = DMA_STANDARD_CTRLA;
	DMA.CH0.CTRLB = DMA_STANDARD_INTERRUPT;
	DMA.CH0.ADDRCTRL = DMA_CH_SRCDIR_FIXED_gc | DMA_CH_DESTDIR_INC_gc | DMA_CH_DESTRELOAD_BLOCK_gc;   //Source reloads after each burst, with byte incrementing.  Dest does not reload, but does increment address.
	DMA.CH0.TRIGSRC = DMA_CH_TRIGSRC_ADCA_CH0_gc;	//Triggered from ADCA channel 0
	DMA.CH0.TRFCNT = DMA_STANDARD_TRANSFER_LENGTH;
	
	DMA.CH0.SRCADDR0 = (( (uint16_t) &ADCA.CH0.RESL) >> 0) & 0xFF; //Source address is ADC
	DMA.CH0.SRCADDR1 = (( (uint16_t) &ADCA.CH0.RESL) >> 8) & 0xFF;
	DMA.CH0.SRCADDR2 = 0x00;
	
	DMA.CH0.DESTADDR0 = (( (uint16_t) &isoBuf[0]) >> 0) & 0xFF;  //Dest address is isoBuf
	DMA.CH0.DESTADDR1 = (( (uint16_t) &isoBuf[0]) >> 8) & 0xFF;
	DMA.CH0.DESTADDR2 = 0x00;
	
				
	DMA.CH1.CTRLA = DMA_STANDARD_CTRLA;
	DMA.CH1.CTRLB = DMA_STANDARD_INTERRUPT;
	DMA.CH1.ADDRCTRL = DMA_CH_SRCDIR_FIXED_gc | DMA_CH_DESTDIR_INC_gc | DMA_CH_DESTRELOAD_BLOCK_gc;   //Source reloads after each burst, with byte incrementing.  Dest does not reload, but does increment address.
	DMA.CH1.TRIGSRC = DMA_CH_TRIGSRC_ADCA_CH2_gc;	//Triggered from ADCA channel 2
	DMA.CH1.TRFCNT = DMA_STANDARD_TRANSFER_LENGTH;
				
	DMA.CH1.SRCADDR0 = (( (uint16_t) &ADCA.CH2.RESL) >> 0) & 0xFF; //Source address is ADC
	DMA.CH1.SRCADDR1 = (( (uint16_t) &ADCA.CH2.RESL) >> 8) & 0xFF;
	DMA.CH1.SRCADDR2 = 0x00;
				
	DMA.CH1.DESTADDR0 = (( (uint16_t) &isoBuf[PACKET_SIZE]) >> 0) & 0xFF;  //Dest address is isoBuf
	DMA.CH1.DESTADDR1 = (( (uint16_t) &isoBuf[PACKET_SIZE]) >> 8) & 0xFF;
	DMA.CH1.DESTADDR2 = 0x00;
				
	//Must enable last for REPCNT won't work!

	tiny_calibration_synchronise_phase(500, 200);
	median_TRFCNT = 200;
	median_TRFCNT_delay = 1; //Wait a few frames before actually setting median_TRFCNT, in case a SOF interrupt was queued during tiny_dma_set_mode_xxx.
	DMA.CH0.CTRLA |= DMA_CH_ENABLE_bm;  //Enable!
	DMA.CH1.CTRLA |= DMA_CH_ENABLE_bm;  //Enable!
	
}

void tiny_dma_loop_mode_2(void){
	return;
}



void tiny_dma_set_mode_3(void){
	global_mode = 3;
	tiny_dma_flush();	

	// TX UART waveform
	if(!repeat_forever)
	{
		DMA.CH3.REPCNT = 1; //Do not repeat
		DMA.CH3.CTRLA = DMA_CH_BURSTLEN_1BYTE_gc | DMA_CH_SINGLE_bm;
	}
	// Remaining waveforms
	else
	{
		DMA.CH3.REPCNT = 0; //Repeat forever!
		DMA.CH3.CTRLA = DMA_CH_BURSTLEN_1BYTE_gc | DMA_CH_SINGLE_bm | DMA_CH_REPEAT_bm;
	}
	DMA.CH3.CTRLB = 0x00; //No interrupt for DacBuf!!
	DMA.CH3.ADDRCTRL = DMA_CH_DESTRELOAD_BURST_gc | DMA_CH_DESTDIR_INC_gc | DMA_CH_SRCRELOAD_BLOCK_gc | DMA_CH_SRCDIR_INC_gc;   //Dest reloads after each burst, with byte incrementing.  Src reloads at end of block, also incrementing address.
	DMA.CH3.TRIGSRC = DMA_CH_TRIGSRC_EVSYS_CH1_gc;	//Triggered from TCC0 when it hits PER
	DMA.CH3.TRFCNT = auxDacBufLen;

	DMA.CH3.SRCADDR0 = (( (uint16_t) &dacBuf_CH2[0]) >> 0) & 0xFF; //Source address is dacbuf
	DMA.CH3.SRCADDR1 = (( (uint16_t) &dacBuf_CH2[0]) >> 8) & 0xFF;
	DMA.CH3.SRCADDR2 = 0x00;
		
	DMA.CH3.DESTADDR0 = (( (uint16_t) &DACB.CH1DATAH) >> 0) & 0xFF;  //Dest address is high byte of DAC register
	DMA.CH3.DESTADDR1 = (( (uint16_t) &DACB.CH1DATAH) >> 8) & 0xFF;
	DMA.CH3.DESTADDR2 = 0x00;
		
	//Must enable last for REPCNT won't work!
	DMA.CH3.CTRLA |= DMA_CH_ENABLE_bm;  //Enable!

	DMA.CH2.REPCNT = 0; //Repeat forever!
	DMA.CH2.CTRLA = DMA_CH_BURSTLEN_1BYTE_gc | DMA_CH_SINGLE_bm | DMA_CH_REPEAT_bm;
	DMA.CH2.CTRLB = 0x00; //Hi interrupt on block complete
	DMA.CH2.ADDRCTRL = DMA_CH_DESTRELOAD_BURST_gc | DMA_CH_DESTDIR_INC_gc | DMA_CH_SRCRELOAD_BLOCK_gc | DMA_CH_SRCDIR_INC_gc;   //Dest reloads after each burst, with byte incrementing.  Src reloads at end of block, also incrementing address.
	DMA.CH2.TRIGSRC = DMA_CH_TRIGSRC_EVSYS_CH2_gc;	//Triggered from TCC0 when it hits PER
	DMA.CH2.TRFCNT = dacBuf_len;

	DMA.CH2.SRCADDR0 = (( (uint16_t) &dacBuf_CH1[0]) >> 0) & 0xFF; //Source address is dacbuf
	DMA.CH2.SRCADDR1 = (( (uint16_t) &dacBuf_CH1[0]) >> 8) & 0xFF;
	DMA.CH2.SRCADDR2 = 0x00;
		
	DMA.CH2.DESTADDR0 = (( (uint16_t) &DACB.CH0DATAH) >> 0) & 0xFF;  //Dest address is high byte of DAC register
	DMA.CH2.DESTADDR1 = (( (uint16_t) &DACB.CH0DATAH) >> 8) & 0xFF;
	DMA.CH2.DESTADDR2 = 0x00;
		
	//Must enable last for REPCNT won't work!
	DMA.CH2.CTRLA |= DMA_CH_ENABLE_bm;  //Enable!

	
	//AUX channel (to keep it tx, therefore always rx)
	DMA.CH1.CTRLA = 0x00;
	DMA.CH1.CTRLA = DMA_CH_RESET_bm;
	
	DMA.CH1.CTRLA = DMA_CH_BURSTLEN_1BYTE_gc | DMA_CH_SINGLE_bm; //Do not repeat!
	DMA.CH1.CTRLB = 0x00;  //No int
	DMA.CH1.ADDRCTRL = DMA_CH_SRCDIR_FIXED_gc | DMA_CH_DESTDIR_FIXED_gc;   //Source and address fixed.
	DMA.CH1.TRIGSRC = DMA_CH_TRIGSRC_USARTC0_RXC_gc;
	DMA.CH1.TRFCNT = 0;
	DMA.CH1.REPCNT = 0;
	
	DMA.CH1.SRCADDR0 = (( (uint16_t) &dummy) >> 0) & 0xFF;
	DMA.CH1.SRCADDR1 = (( (uint16_t) &dummy) >> 8) & 0xFF;
	DMA.CH1.SRCADDR2 = 0x00;
	
	DMA.CH1.DESTADDR0 = (( (uint16_t) &USARTC0.DATA) >> 0) & 0xFF;
	DMA.CH1.DESTADDR1 = (( (uint16_t) &USARTC0.DATA) >> 8) & 0xFF;
	DMA.CH1.DESTADDR2 = 0x00;
	
	//Must enable last for REPCNT won't work!
	DMA.CH1.CTRLA |= DMA_CH_REPEAT_bm | DMA_CH_ENABLE_bm;  //Enable!
	
	USARTC0.DATA = 0x55;
		
	//Actual data being transferred
	DMA.CH0.CTRLA = 0x00;
	DMA.CH0.CTRLA = DMA_CH_RESET_bm;
		
	DMA.CH0.CTRLA = DMA_STANDARD_CTRLA;
	DMA.CH0.CTRLB = DMA_STANDARD_INTERRUPT;
	DMA.CH0.ADDRCTRL = DMA_CH_SRCRELOAD_BURST_gc | DMA_CH_SRCDIR_INC_gc | DMA_CH_DESTDIR_INC_gc | DMA_CH_DESTRELOAD_BLOCK_gc;   //Source reloads after each burst, with byte incrementing.  Dest does not reload, but does increment address.
	DMA.CH0.TRIGSRC = DMA_CH_TRIGSRC_USARTC0_RXC_gc;
	DMA.CH0.TRFCNT = DMA_STANDARD_TRANSFER_LENGTH;
		
	DMA.CH0.SRCADDR0 = (( (uint16_t) &USARTC0.DATA) >> 0) & 0xFF; //Source address is ADC
	DMA.CH0.SRCADDR1 = (( (uint16_t) &USARTC0.DATA) >> 8) & 0xFF;
	DMA.CH0.SRCADDR2 = 0x00;
		
	DMA.CH0.DESTADDR0 = (( (uint16_t) &isoBuf[0]) >> 0) & 0xFF;  //Dest address is isoBuf
	DMA.CH0.DESTADDR1 = (( (uint16_t) &isoBuf[0]) >> 8) & 0xFF;
	DMA.CH0.DESTADDR2 = 0x00;
		
	tiny_calibration_synchronise_phase(500, 200);
	median_TRFCNT = 200;
	median_TRFCNT_delay = 1; //Wait a few frames before actually setting median_TRFCNT, in case a SOF interrupt was queued during tiny_dma_set_mode_xxx.

	//Must enable last for REPCNT won't work!
	DMA.CH0.CTRLA |= DMA_CH_ENABLE_bm;  //Enable!
}

void tiny_dma_loop_mode_3(void){
	return;
}

void tiny_dma_set_mode_4(void){
	
	global_mode = 4;
	
	tiny_dma_flush();
	
	//AUX channel (to keep it tx, therefore always rx)
	DMA.CH2.CTRLA = 0x00;
	DMA.CH2.CTRLA = DMA_CH_RESET_bm;

	DMA.CH2.CTRLA = DMA_CH_BURSTLEN_1BYTE_gc | DMA_CH_SINGLE_bm; //Do not repeat!
	DMA.CH2.CTRLB = 0x00;  //No int
	DMA.CH2.ADDRCTRL = DMA_CH_SRCDIR_FIXED_gc | DMA_CH_DESTDIR_FIXED_gc;   //Source and address fixed.
	DMA.CH2.TRIGSRC = DMA_CH_TRIGSRC_USARTC0_RXC_gc;
	DMA.CH2.TRFCNT = 0;
	DMA.CH2.REPCNT = 0;
	
	DMA.CH2.SRCADDR0 = (( (uint16_t) &dummy) >> 0) & 0xFF;
	DMA.CH2.SRCADDR1 = (( (uint16_t) &dummy) >> 8) & 0xFF;
	DMA.CH2.SRCADDR2 = 0x00;
	
	DMA.CH2.DESTADDR0 = (( (uint16_t) &USARTC0.DATA) >> 0) & 0xFF;
	DMA.CH2.DESTADDR1 = (( (uint16_t) &USARTC0.DATA) >> 8) & 0xFF;
	DMA.CH2.DESTADDR2 = 0x00;
	
	//Must enable last for REPCNT won't work!
	DMA.CH2.CTRLA |= DMA_CH_REPEAT_bm | DMA_CH_ENABLE_bm;  //Enable!
	
	USARTC0.DATA = 0x55;
	
	// TX UART waveform
	if(!repeat_forever)
	{
		DMA.CH3.REPCNT = 1; //Do not repeat
		DMA.CH3.CTRLA = DMA_CH_BURSTLEN_1BYTE_gc | DMA_CH_SINGLE_bm;
	}
	// Remaining waveforms
	else
	{
		DMA.CH3.REPCNT = 0; //Repeat forever!
		DMA.CH3.CTRLA = DMA_CH_BURSTLEN_1BYTE_gc | DMA_CH_SINGLE_bm | DMA_CH_REPEAT_bm;
	}
	DMA.CH3.CTRLB = 0x00; //No interrupt for DacBuf!!
	DMA.CH3.ADDRCTRL = DMA_CH_DESTRELOAD_BURST_gc | DMA_CH_DESTDIR_INC_gc | DMA_CH_SRCRELOAD_BLOCK_gc | DMA_CH_SRCDIR_INC_gc;   //Dest reloads after each burst, with byte incrementing.  Src reloads at end of block, also incrementing address.
	DMA.CH3.TRIGSRC = DMA_CH_TRIGSRC_EVSYS_CH1_gc;	//Triggered from TCC0 when it hits PER
	DMA.CH3.TRFCNT = auxDacBufLen;

	DMA.CH3.SRCADDR0 = (( (uint16_t) &dacBuf_CH2[0]) >> 0) & 0xFF; //Source address is dacbuf
	DMA.CH3.SRCADDR1 = (( (uint16_t) &dacBuf_CH2[0]) >> 8) & 0xFF;
	DMA.CH3.SRCADDR2 = 0x00;
	
	DMA.CH3.DESTADDR0 = (( (uint16_t) &DACB.CH1DATAH) >> 0) & 0xFF;  //Dest address is high byte of DAC register
	DMA.CH3.DESTADDR1 = (( (uint16_t) &DACB.CH1DATAH) >> 8) & 0xFF;
	DMA.CH3.DESTADDR2 = 0x00;
	
	//Must enable last for REPCNT won't work!
	DMA.CH3.CTRLA |= DMA_CH_ENABLE_bm;  //Enable!
	
	//Actual data being transferred
	DMA.CH0.CTRLA = DMA_STANDARD_CTRLA;
	DMA.CH0.CTRLB = DMA_STANDARD_INTERRUPT;
	DMA.CH0.ADDRCTRL = DMA_CH_SRCRELOAD_BURST_gc | DMA_CH_SRCDIR_INC_gc | DMA_CH_DESTDIR_INC_gc | DMA_CH_DESTRELOAD_BLOCK_gc;   //Source reloads after each burst, with byte incrementing.  Dest does not reload, but does increment address.
	DMA.CH0.TRIGSRC = DMA_CH_TRIGSRC_USARTC0_RXC_gc;
	DMA.CH0.TRFCNT = DMA_STANDARD_TRANSFER_LENGTH;
		
	DMA.CH0.SRCADDR0 = (( (uint16_t) &USARTC0.DATA) >> 0) & 0xFF; //Source address is ADC
	DMA.CH0.SRCADDR1 = (( (uint16_t) &USARTC0.DATA) >> 8) & 0xFF;
	DMA.CH0.SRCADDR2 = 0x00;
		
	DMA.CH0.DESTADDR0 = (( (uint16_t) &isoBuf[0]) >> 0) & 0xFF;  //Dest address is isoBuf
	DMA.CH0.DESTADDR1 = (( (uint16_t) &isoBuf[0]) >> 8) & 0xFF;
	DMA.CH0.DESTADDR2 = 0x00;
		
		
	DMA.CH1.CTRLA = DMA_STANDARD_CTRLA;
	DMA.CH1.CTRLB = DMA_STANDARD_INTERRUPT;
	DMA.CH1.ADDRCTRL = DMA_CH_SRCRELOAD_BURST_gc | DMA_CH_SRCDIR_INC_gc | DMA_CH_DESTDIR_INC_gc | DMA_CH_DESTRELOAD_BLOCK_gc;   //Source reloads after each burst, with byte incrementing.  Dest does not reload, but does increment address.
	DMA.CH1.TRIGSRC = DMA_CH_TRIGSRC_SPIC_gc;
	DMA.CH1.TRFCNT = DMA_STANDARD_TRANSFER_LENGTH;
		
	DMA.CH1.SRCADDR0 = (( (uint16_t) &SPIC.DATA) >> 0) & 0xFF; //Source address is ADC
	DMA.CH1.SRCADDR1 = (( (uint16_t) &SPIC.DATA) >> 8) & 0xFF;
	DMA.CH1.SRCADDR2 = 0x00;
		
	DMA.CH1.DESTADDR0 = (( (uint16_t) &isoBuf[PACKET_SIZE]) >> 0) & 0xFF;  //Dest address is isoBuf
	DMA.CH1.DESTADDR1 = (( (uint16_t) &isoBuf[PACKET_SIZE]) >> 8) & 0xFF;
	DMA.CH1.DESTADDR2 = 0x00;
		
	tiny_calibration_synchronise_phase(500, 200);
	median_TRFCNT = 200;
	median_TRFCNT_delay = 1; //Wait a few frames before actually setting median_TRFCNT, in case a SOF interrupt was queued during tiny_dma_set_mode_xxx.
	DMA.CH0.CTRLA |= DMA_CH_ENABLE_bm;  //Enable!
	DMA.CH1.CTRLA |= DMA_CH_ENABLE_bm;  //Enable!

	// Hack: manually trigger a transfer to ensure zero phase difference.
	DMA.CH0.CTRLA |= DMA_CH_TRFREQ_bm;
}

void tiny_dma_loop_mode_4(void){
return;
}
	
	
void tiny_dma_set_mode_5(void){
	while(1); //Deliberate Crash!  Mode 5 should be invalid.
}

void tiny_dma_set_mode_6(void){
		
	global_mode = 6;
	
	tiny_dma_flush();
	
	DMA.CH2.REPCNT = 0; //Repeat forever!
	DMA.CH2.CTRLA = DMA_CH_BURSTLEN_1BYTE_gc | DMA_CH_SINGLE_bm | DMA_CH_REPEAT_bm;
	DMA.CH2.CTRLB = 0x00; //Hi interrupt on block complete
	DMA.CH2.ADDRCTRL = DMA_CH_DESTRELOAD_BURST_gc | DMA_CH_DESTDIR_INC_gc | DMA_CH_SRCRELOAD_BLOCK_gc | DMA_CH_SRCDIR_INC_gc;   //Dest reloads after each burst, with byte incrementing.  Src reloads at end of block, also incrementing address.
	DMA.CH2.TRIGSRC = DMA_CH_TRIGSRC_EVSYS_CH2_gc;	//Triggered from TCC0 when it hits PER
	DMA.CH2.TRFCNT = dacBuf_len;

	DMA.CH2.SRCADDR0 = (( (uint16_t) &dacBuf_CH1[0]) >> 0) & 0xFF; //Source address is dacbuf
	DMA.CH2.SRCADDR1 = (( (uint16_t) &dacBuf_CH1[0]) >> 8) & 0xFF;
	DMA.CH2.SRCADDR2 = 0x00;
	
	DMA.CH2.DESTADDR0 = (( (uint16_t) &DACB.CH0DATAH) >> 0) & 0xFF;  //Dest address is high byte of DAC register
	DMA.CH2.DESTADDR1 = (( (uint16_t) &DACB.CH0DATAH) >> 8) & 0xFF;
	DMA.CH2.DESTADDR2 = 0x00;
	
	//Must enable last for REPCNT won't work!
	DMA.CH2.CTRLA |= DMA_CH_ENABLE_bm;  //Enable!
	
	// TX UART waveform
	if(!repeat_forever)
	{
		DMA.CH3.REPCNT = 1; //Do not repeat
		DMA.CH3.CTRLA = DMA_CH_BURSTLEN_1BYTE_gc | DMA_CH_SINGLE_bm;
	}
	// Remaining waveforms
	else
	{
		DMA.CH3.REPCNT = 0; //Repeat forever!
		DMA.CH3.CTRLA = DMA_CH_BURSTLEN_1BYTE_gc | DMA_CH_SINGLE_bm | DMA_CH_REPEAT_bm;
	}
	DMA.CH3.CTRLB = 0x00; //No interrupt for DacBuf!!
	DMA.CH3.ADDRCTRL = DMA_CH_DESTRELOAD_BURST_gc | DMA_CH_DESTDIR_INC_gc | DMA_CH_SRCRELOAD_BLOCK_gc | DMA_CH_SRCDIR_INC_gc;   //Dest reloads after each burst, with byte incrementing.  Src reloads at end of block, also incrementing address.
	DMA.CH3.TRIGSRC = DMA_CH_TRIGSRC_EVSYS_CH1_gc;	//Triggered from TCC0 when it hits PER
	DMA.CH3.TRFCNT = auxDacBufLen;

	DMA.CH3.SRCADDR0 = (( (uint16_t) &dacBuf_CH2[0]) >> 0) & 0xFF; //Source address is dacbuf
	DMA.CH3.SRCADDR1 = (( (uint16_t) &dacBuf_CH2[0]) >> 8) & 0xFF;
	DMA.CH3.SRCADDR2 = 0x00;
	
	DMA.CH3.DESTADDR0 = (( (uint16_t) &DACB.CH1DATAH) >> 0) & 0xFF;  //Dest address is high byte of DAC register
	DMA.CH3.DESTADDR1 = (( (uint16_t) &DACB.CH1DATAH) >> 8) & 0xFF;
	DMA.CH3.DESTADDR2 = 0x00;
	
	//Must enable last for REPCNT won't work!
	DMA.CH3.CTRLA |= DMA_CH_ENABLE_bm;  //Enable!
	
	DMA.CH0.CTRLA = 0x00;
	DMA.CH0.CTRLA = DMA_CH_RESET_bm;
		
	DMA.CH0.CTRLA = DMA_CH_BURSTLEN_1BYTE_gc | DMA_CH_SINGLE_bm | DMA_CH_REPEAT_bm;
	DMA.CH0.CTRLB = 0x00; //No interrupt!
	DMA.CH0.ADDRCTRL = DMA_CH_SRCRELOAD_BURST_gc | DMA_CH_SRCDIR_INC_gc | DMA_CH_DESTDIR_INC_gc | DMA_CH_DESTRELOAD_BLOCK_gc;   //Source reloads after each burst, with byte incrementing.  Dest does not reload, but does increment address.
	DMA.CH0.TRIGSRC = DMA_CH_TRIGSRC_ADCA_CH0_gc;	//Triggered from ADCA channel 0
	DMA.CH0.TRFCNT = BUFFER_SIZE;
		
	DMA.CH0.SRCADDR0 = (( (uint16_t) &ADCA.CH0.RESL) >> 0) & 0xFF; //Source address is ADC
	DMA.CH0.SRCADDR1 = (( (uint16_t) &ADCA.CH0.RESL) >> 8) & 0xFF;
	DMA.CH0.SRCADDR2 = 0x00;
		
	DMA.CH0.DESTADDR0 = (( (uint16_t) &isoBuf[0]) >> 0) & 0xFF;  //Dest address is isoBuf
	DMA.CH0.DESTADDR1 = (( (uint16_t) &isoBuf[0]) >> 8) & 0xFF;
	DMA.CH0.DESTADDR2 = 0x00;
		
	tiny_calibration_synchronise_phase(500, 200);
	median_TRFCNT = 400;
	median_TRFCNT_delay = 1; //Wait a few frames before actually setting median_TRFCNT, in case a SOF interrupt was queued during tiny_dma_set_mode_xxx.
	DMA.CH0.CTRLA |= DMA_CH_ENABLE_bm;  //Enable!	
	
	
}

void tiny_dma_loop_mode_6(void){
	return;
}

void tiny_dma_set_mode_7(void){
				
		global_mode = 7;
		
		tiny_dma_flush();
		
		DMA.CH2.REPCNT = 0; //Repeat forever!
		DMA.CH2.CTRLA = DMA_CH_BURSTLEN_1BYTE_gc | DMA_CH_SINGLE_bm | DMA_CH_REPEAT_bm;
		DMA.CH2.CTRLB = 0x00; //Hi interrupt on block complete
		DMA.CH2.ADDRCTRL = DMA_CH_DESTRELOAD_BURST_gc | DMA_CH_DESTDIR_INC_gc | DMA_CH_SRCRELOAD_BLOCK_gc | DMA_CH_SRCDIR_INC_gc;   //Dest reloads after each burst, with byte incrementing.  Src reloads at end of block, also incrementing address.
		DMA.CH2.TRIGSRC = DMA_CH_TRIGSRC_EVSYS_CH2_gc;	//Triggered from TCC0 when it hits PER
		DMA.CH2.TRFCNT = dacBuf_len;

		DMA.CH2.SRCADDR0 = (( (uint16_t) &dacBuf_CH1[0]) >> 0) & 0xFF; //Source address is dacbuf
		DMA.CH2.SRCADDR1 = (( (uint16_t) &dacBuf_CH1[0]) >> 8) & 0xFF;
		DMA.CH2.SRCADDR2 = 0x00;
			
		DMA.CH2.DESTADDR0 = (( (uint16_t) &DACB.CH0DATAH) >> 0) & 0xFF;  //Dest address is high byte of DAC register
		DMA.CH2.DESTADDR1 = (( (uint16_t) &DACB.CH0DATAH) >> 8) & 0xFF;
		DMA.CH2.DESTADDR2 = 0x00;
			
		//Must enable last for REPCNT won't work!
		DMA.CH2.CTRLA |= DMA_CH_ENABLE_bm;  //Enable!
		
		// TX UART waveform
		if(!repeat_forever)
		{
			DMA.CH3.REPCNT = 1; //Do not repeat
			DMA.CH3.CTRLA = DMA_CH_BURSTLEN_1BYTE_gc | DMA_CH_SINGLE_bm;
		}
		// Remaining waveforms
		else
		{
			DMA.CH3.REPCNT = 0; //Repeat forever!
			DMA.CH3.CTRLA = DMA_CH_BURSTLEN_1BYTE_gc | DMA_CH_SINGLE_bm | DMA_CH_REPEAT_bm;
		}
		DMA.CH3.CTRLB = 0x00; //No interrupt for DacBuf!!
		DMA.CH3.ADDRCTRL = DMA_CH_DESTRELOAD_BURST_gc | DMA_CH_DESTDIR_INC_gc | DMA_CH_SRCRELOAD_BLOCK_gc | DMA_CH_SRCDIR_INC_gc;   //Dest reloads after each burst, with byte incrementing.  Src reloads at end of block, also incrementing address.
		DMA.CH3.TRIGSRC = DMA_CH_TRIGSRC_EVSYS_CH1_gc;	//Triggered from TCC0 when it hits PER
		DMA.CH3.TRFCNT = auxDacBufLen;

		DMA.CH3.SRCADDR0 = (( (uint16_t) &dacBuf_CH2[0]) >> 0) & 0xFF; //Source address is dacbuf
		DMA.CH3.SRCADDR1 = (( (uint16_t) &dacBuf_CH2[0]) >> 8) & 0xFF;
		DMA.CH3.SRCADDR2 = 0x00;
		
		DMA.CH3.DESTADDR0 = (( (uint16_t) &DACB.CH1DATAH) >> 0) & 0xFF;  //Dest address is high byte of DAC register
		DMA.CH3.DESTADDR1 = (( (uint16_t) &DACB.CH1DATAH) >> 8) & 0xFF;
		DMA.CH3.DESTADDR2 = 0x00;
		
		//Must enable last for REPCNT won't work!
		DMA.CH3.CTRLA |= DMA_CH_ENABLE_bm;  //Enable!	
		
		DMA.CH0.CTRLA = 0x00;
		DMA.CH0.CTRLA = DMA_CH_RESET_bm;
				
		DMA.CH0.CTRLA = DMA_CH_BURSTLEN_2BYTE_gc | DMA_CH_SINGLE_bm | DMA_CH_REPEAT_bm; //Do not repeat!
		DMA.CH0.CTRLB = 0x00; //No interrupt!
		DMA.CH0.ADDRCTRL = DMA_CH_SRCRELOAD_BURST_gc | DMA_CH_SRCDIR_INC_gc | DMA_CH_DESTDIR_INC_gc | DMA_CH_DESTRELOAD_BLOCK_gc;   //Source reloads after each burst, with byte incrementing.  Dest does not reload, but does increment address.
		DMA.CH0.TRIGSRC = DMA_CH_TRIGSRC_ADCA_CH0_gc;	//Triggered from ADCA channel 0
		DMA.CH0.TRFCNT = BUFFER_SIZE;
				
		DMA.CH0.SRCADDR0 = (( (uint16_t) &ADCA.CH0.RESL) >> 0) & 0xFF; //Source address is ADC
		DMA.CH0.SRCADDR1 = (( (uint16_t) &ADCA.CH0.RESL) >> 8) & 0xFF;
		DMA.CH0.SRCADDR2 = 0x00;
				
		DMA.CH0.DESTADDR0 = (( (uint16_t) &isoBuf[0]) >> 0) & 0xFF;  //Dest address is isoBuf
		DMA.CH0.DESTADDR1 = (( (uint16_t) &isoBuf[0]) >> 8) & 0xFF;
		DMA.CH0.DESTADDR2 = 0x00;
				
		tiny_calibration_synchronise_phase(500, 200);
		median_TRFCNT = 400;
		median_TRFCNT_delay = 1; //Wait a few frames before actually setting median_TRFCNT, in case a SOF interrupt was queued during tiny_dma_set_mode_xxx.
		DMA.CH0.CTRLA |= DMA_CH_ENABLE_bm;  //Enable!	
		
}

void tiny_dma_loop_mode_7(void){
}

ISR(DMA_CH0_vect){
	DMA.INTFLAGS = 0x01;
	//dma_ch0_ran++;
	//uds.dma_ch0_cntL = dma_ch0_ran & 0xff;
	//uds.dma_ch0_cntH = (dma_ch0_ran >> 8) & 0xff;
	
	#ifdef SINGLE_ENDPOINT_INTERFACE
	DMA.CH0.CTRLA = 0x00;	
	DMA.CH0.CTRLA = DMA_CH_BURSTLEN_1BYTE_gc | DMA_CH_SINGLE_bm; //Do not repeat!
	DMA.CH0.TRFCNT = HALFPACKET_SIZE;
			
	short ptr = usb_state ? 0 : PACKET_SIZE;
			
	DMA.CH0.DESTADDR0 = (( (uint16_t) &isoBuf[ptr]) >> 0) & 0xFF;  //Dest address is isoBuf
	DMA.CH0.DESTADDR1 = (( (uint16_t) &isoBuf[ptr]) >> 8) & 0xFF;

	DMA.CH0.CTRLA |= DMA_CH_ENABLE_bm;
	#endif
}

ISR(DMA_CH1_vect){
	DMA.INTFLAGS = 0x02;
	//dma_ch1_ran++;
	//uds.dma_ch1_cntL = dma_ch1_ran & 0xff;
	//uds.dma_ch1_cntH = (dma_ch1_ran >> 8) & 0xff;

#ifdef SINGLE_ENDPOINT_INTERFACE
	DMA.CH1.CTRLA = 0x00;
	DMA.CH1.CTRLA = DMA_CH_BURSTLEN_1BYTE_gc | DMA_CH_SINGLE_bm; //Do not repeat!
	DMA.CH1.TRFCNT = HALFPACKET_SIZE;
	
	short ptr = usb_state ? HALFPACKET_SIZE : PACKET_SIZE + HALFPACKET_SIZE;
	
	DMA.CH1.DESTADDR0 = (( (uint16_t) &isoBuf[ptr]) >> 0) & 0xFF;  //Dest address is isoBuf
	DMA.CH1.DESTADDR1 = (( (uint16_t) &isoBuf[ptr]) >> 8) & 0xFF;

	DMA.CH1.CTRLA |= DMA_CH_ENABLE_bm;
	#endif
}
