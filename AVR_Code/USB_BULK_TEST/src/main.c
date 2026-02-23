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

#ifdef SINGLE_ENDPOINT_INTERFACE
	const unsigned char variant = 0x02;
#else
	const unsigned char variant = 0x01;
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
	if(eeprom_buffer_read[0]){
			memcpy(eeprom_buffer_write, eeprom_buffer_read, EEPROM_PAGE_SIZE);
			eeprom_buffer_write[0] = 0;
			eeprom_safe_write();
			//eeprom_safe_read();
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
	#ifndef SINGLE_ENDPOINT_INTERFACE
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
}

void main_vendor_disable(void)
{
	main_b_vendor_enable = false;
}

bool main_setup_out_received(void)
{
	return 1;
}

bool main_setup_in_received(void)
{
	return true;
}

void iso_callback(udd_ep_status_t status, iram_size_t nb_transfered, udd_ep_id_t ep){
	#ifndef SINGLE_ENDPOINT_INTERFACE
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
