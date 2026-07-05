/*
 * tiny_eeprom.h
 *
 * Created: 22/04/2017 12:37:34 PM
 *  Author: Esposch
 */ 


#ifndef TINY_EEPROM_H_
#define TINY_EEPROM_H_

#define EEPROM_CURRENT_PAGE 1
//Calibration storage page (vendor requests 0xac/0xad).  Kept separate from
//the bootloader-flag page above.  NOTE: a DFU chip erase wipes EEPROM, so
//the host keeps its own copy of the calibration and should re-save it to
//the device after a firmware update.
#define EEPROM_CAL_PAGE 2

void eeprom_safe_read();
void eeprom_safe_write();
void eeprom_cal_read();
void eeprom_cal_write();
extern volatile unsigned char eeprom_buffer_write[EEPROM_PAGE_SIZE];
extern volatile unsigned char eeprom_buffer_read[EEPROM_PAGE_SIZE];
extern volatile unsigned char eeprom_cal_buffer[EEPROM_PAGE_SIZE];


#endif /* TINY_EEPROM_H_ */