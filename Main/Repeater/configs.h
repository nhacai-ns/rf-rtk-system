#pragma once

#ifndef CONFIG_H
#define CONFIG_H

#include <SPI.h>
#include <RF24.h>
#include <nRF24L01.h>

#include <ArduinoJson.h>
#include <Arduino.h>
#include <Wire.h>
#include "IWatchdog.h"
#include "stm32f4xx_hal.h"

// macro 
#define DEBUG_
#define ROVER_REPEATER

#undef PIN_SPI_MOSI
#undef PIN_SPI_MISO
#undef PIN_SPI_SCK
#undef PIN_SPI_SS
#define PIN_SPI_MOSI  PA7
#define PIN_SPI_MISO  PA6
#define PIN_SPI_SCK   PA5
#define PIN_SPI_SS    PA4 

#define LED_BUILTIN PC13
#define RF_CE   PA12
#define RF_CSN  PA11
#define RF_SPI_MOSI PB15
#define RF_SPI_MISO PB14
#define RF_SPI_SCK PB10
#define SDA_PIN PB7
#define SCL_PIN PB6

#define TX_LOG PA9
#define RX_LOG PA10

#define RF_SPI_IRQ PA8

// rf 
#define RF_DATA_RATE RF24_250KBPS // RF24_250KBPS, RF24_1MBPS, RF24_2MBPS
#define RF_PA RF24_PA_MAX // RF24_PA_MIN, RF24_PA_LOW, RF24_PA_HIGH, RF24_PA_MAX
#define RF_CHANNEL 50

#define STATUS_TIMER TIM3

// connection
#define REPORT_TIMEOUT_SEC 10

#define ROVER_TIMEOUT_MS 10000
#define RF_RTCM_CHUNK_DATA_SIZE 25

#ifdef BASE_REPEATER
const uint64_t ADDR_REPEATER = 0xAABBCCDD11LL;
#else
const uint64_t ADDR_REPEATER = 0xAABBCCDD21LL;
#endif

struct RF_Rover_Report {
  uint8_t device_id;   // ID của Rover
  uint8_t type;       // 0xB1 (Nhãn nhận diện)
  uint32_t time;      // Thời gian GPS (HHMMSS)
  int64_t lat;        // Vĩ độ nhân 10^7
  int64_t lon;        // Kinh độ nhân 10^7
  uint8_t battery;    // Phần trăm pin
  uint8_t modeRTK;    // Chế độ RTK (0, 1, 2, 4, 5...)
  uint8_t typeButton;
};

struct RF_RTCM_Chunk {
  uint8_t device_id;
  uint8_t type;
  uint8_t batch_id;
  uint8_t seq;
  uint8_t total;
  uint8_t data[RF_RTCM_CHUNK_DATA_SIZE];
};

enum TYPE_Packet_Enum {
  TYPE_RTCM,
  TYPE_REPORT,
  TYPE_REPORT_REPEATED,
  TYPE_BUTTON_PRESSED,
  TYPE_NOTI,
  TYPE_MSG,
  TYPE_PING,
  TYPE_RTCM_REPEATED
};

enum STATUS_Enum {
  STATUS_DISCONNECT,
  STATUS_CONNECT,
  STATUS_LOW_BATTERRY,
};

#endif 