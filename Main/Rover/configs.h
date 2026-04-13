#pragma once

#ifndef CONFIG_H
#define CONFIG_H

#include <SPI.h>
#include <RF24.h>
#include <nRF24L01.h>

#include <ArduinoJson.h>
#include <Arduino.h>
#include <Wire.h>
#include <stdint.h>
#include <stdbool.h>
#include "stm32f4xx_hal.h"

// macro 
#define DEBUG_

#define DEVICE_ID 4
#define SLOT_STEP_TICKS 1      // ID range 1 tick (100ms)
#define TOTAL_ROVERS    5     // Rover total
#define CYCLE_TICKS     (TOTAL_ROVERS * SLOT_STEP_TICKS) // 5 ticks = 500 ms

#define LED_BUILTIN PA1
#define RF_CE   PA12
#define RF_CSN  PA11
#define RF_SPI_MOSI PB15
#define RF_SPI_MISO PB14
#define RF_SPI_SCK PB10
#define BUZZER_PIN PB9
#define BUTTON_PIN PB8
#define SDA_PIN PB7
#define SCL_PIN PB6

#define TX_LOG PA9
#define RX_LOG PA10

#define RF_SPI_IRQ PA8

// rf 
#define RF_DATA_RATE RF24_250KBPS // RF24_250KBPS, RF24_1MBPS, RF24_2MBPS
#define RF_PA RF24_PA_MIN // RF24_PA_MIN, RF24_PA_LOW, RF24_PA_HIGH, RF24_PA_MAX
#define RF_CHANNEL 50
#define RTCM_IDLE_GAP_MS 50
#define RF_RTCM_CHUNK_DATA_SIZE 25

// uart
#define TX_STORAGE_SIZE 2048 
#define DMA_TX_TEMP_SIZE    1024 
#define SERIAL_LOG_BAUDRATE 115200

// connection
#define RTCM_TIMEOUT_SEC 10

// time 
#define LAST_REPORT_MS 1000
#define LAST_RF_RX_MS 50
#define STATUS_TIMER TIM3
#define BUZZER_TIMER TIM4 // TIM1 
#define BUZZER_NOTI_FREQ 5000
#define MAX_BUTTON_RETRY  5
#define BUTTON_RETRY_INTERVAL  200
#define MAX_DEBOUNCE_TIME  300

// battery
#define MAX17048_ADDR 0x36
#define LOW_BATTERY_THRESHOLD  5
#define LAST_CHECK_BATTERY 1000

// base 2 option
#define LAST_BASE_TIMEOUT 5000

const uint64_t ADDR_BASE_TO_ALL = 0xAABBCCDD11LL;
const uint64_t ADDR_ROVER_TO_BASE = 0xAABBCCDD21LL;

struct RF_RTCM_Chunk {
  uint8_t device_id;
  uint8_t type;
  uint8_t batch_id;
  uint8_t seq;
  uint8_t total;
  uint8_t data[RF_RTCM_CHUNK_DATA_SIZE];
};

struct RF_Rover_Report {
  uint8_t device_id;
  uint8_t type;
  uint32_t time;
  int64_t lat;
  int64_t lon;
  uint8_t battery;
  uint8_t modeRTK;
  uint8_t typeButton;
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
  STATUS_MODE_FLOAT,
  STATUS_MODE_FIXED
};

enum RTK_MODE {
  RTK_MODE_INVALID,
  RTK_MODE_GPS_SPS,
  RTK_MODE_DIFF_GPS,
  RTK_MODE_GPS_PPS,
  RTK_MODE_FIXED,
  RTK_MODE_FLOAT,
  RTK_MODE_ESTIMATED
};

enum BUTTON_STATUS {
  BTN_STT_RELEASE,
  BTN_STT_PRESSED
};

// uart
#define UART_RX_BUFFER_SIZE 1024

#endif 