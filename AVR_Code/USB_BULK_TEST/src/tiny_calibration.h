/*
 * tiny_calibration.h
 *
 * Created: 9/01/2017 4:21:09 PM
 *  Author: Esposch
 */ 


#ifndef TINY_CALIBRATION_H_
#define TINY_CALIBRATION_H_

#include <asf.h>
#include <stdio.h>

void tiny_calibration_init();
void tiny_calibration_first_sof();
void tiny_calibration_maintain();
void tiny_calibration_safe_add(int rawValue);
int tiny_distance_from_centre(unsigned int point);
void tiny_calibration_find_values();
void tiny_calibration_layer2();
unsigned int magnitude_difference(unsigned int a, unsigned int b);
void tiny_calibration_synchronise_phase(unsigned int phase, unsigned int precision);

extern volatile unsigned char calibration_values_found;
extern volatile unsigned char median_TRFCNT_delay;

extern volatile unsigned char cali_value_negative_gradient;
extern volatile unsigned char cali_value_positive_gradient;


#endif /* TINY_CALIBRATION_H_ */
