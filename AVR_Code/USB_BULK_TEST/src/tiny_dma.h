/*
 * tiny_dma.h
 *
 * Created: 25/06/2015 9:01:15 AM
 *  Author: Esposch
 */ 


#ifndef TINY_DMA_H_
#define TINY_DMA_H_

#include <stdio.h>
#include <asf.h>
#include "globals.h"

void tiny_dma_setup(void);
void tiny_dma_flush(void);

void tiny_dma_set_mode_0(void);
void tiny_dma_loop_mode_0(void);

void tiny_dma_set_mode_1(void);
void tiny_dma_loop_mode_1(void);

void tiny_dma_set_mode_2(void);
void tiny_dma_loop_mode_2(void);

void tiny_dma_set_mode_3(void);
void tiny_dma_loop_mode_3(void);

void tiny_dma_set_mode_4(void);
void tiny_dma_loop_mode_4(void);

void tiny_dma_set_mode_5(void);

void tiny_dma_set_mode_6(void);
void tiny_dma_loop_mode_6(void);

void tiny_dma_set_mode_7(void);
void tiny_dma_loop_mode_7(void);

void tiny_dma_delayed_set(unsigned char mode);

#ifdef AIO_INTERFACE
void tiny_dma_apply_transport(void);
#endif


#endif /* TINY_DMA_H_ */