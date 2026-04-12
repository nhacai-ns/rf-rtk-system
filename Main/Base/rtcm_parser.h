#pragma once

#ifndef RTCM_PARSER_H
#define RTCM_PARSER_H

#include "configs.h"

void RTCM_Process(const uint8_t *data, uint16_t length);
bool RTCM_GetPacket(uint8_t *out, uint16_t *out_len);

#endif