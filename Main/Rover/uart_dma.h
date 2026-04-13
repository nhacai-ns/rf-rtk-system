#pragma once
#ifndef UART_DMA_H
#define UART_DMA_H

#include "configs.h"

#ifdef __cplusplus
extern "C" {
#endif

void UART_DMA_Init(void);
uint16_t UART_Read(uint8_t *dest, uint16_t maxlen);
void UART_Write_Queue(uint8_t *pData, uint16_t Size);
// run in loop
void UART_DMA_Process_TX();

extern volatile bool uart_idle_flag;

#ifdef __cplusplus
}
#endif

#endif
