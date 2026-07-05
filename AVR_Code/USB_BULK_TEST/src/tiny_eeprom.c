/*
 * tiny_eeprom.c
 *
 * Created: 22/04/2017 12:37:21 PM
 *  Author: Esposch
 */ 
#include <asf.h>
#include "tiny_eeprom.h"

void eeprom_safe_read(){
	unsigned char previous_clk_settings = CLK.PSCTRL;
	CLK.PSCTRL = CLK_PSADIV4_bm; //Slow down the clock to prevent EEPROM misses
	nvm_eeprom_read_buffer(EEPROM_CURRENT_PAGE*EEPROM_PAGE_SIZE, eeprom_buffer_read, EEPROM_PAGE_SIZE);
	CLK.PSCTRL = previous_clk_settings;
	return;
}

void eeprom_safe_write(){
	unsigned char previous_clk_settings = CLK.PSCTRL;
	CLK.PSCTRL = CLK_PSADIV4_bm; //Slow down the clock to prevent EEPROM misses
	nvm_eeprom_load_page_to_buffer(eeprom_buffer_write);
	nvm_eeprom_atomic_write_page(EEPROM_CURRENT_PAGE);
	nvm_wait_until_ready(); //The write must be committed before the caller
	                        //resets or jumps to the bootloader, otherwise the
	                        //bootloader-request flag survives an extra launch.
	CLK.PSCTRL = previous_clk_settings;
	return;
}
