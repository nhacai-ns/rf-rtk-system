#pragma once

#ifndef CONFIG_H
#define CONFIG_H

#include <SPI.h>
#include <RF24.h>
#include <nRF24L01.h>

#include <Ethernet.h>
#include <EthernetUdp.h>

#include <ArduinoJson.h>
#include <Arduino.h>
#include <utility/w5100.h>
#include <deque>
#include "IWatchdog.h"
#include "stm32f4xx_hal.h"

// macro 
#define DEBUG_
#define CONFIG_RTK
#define LAN
#define ACK_

#define DEVICE_ID 0

#undef PIN_SPI_MOSI
#undef PIN_SPI_MISO
#undef PIN_SPI_SCK
#undef PIN_SPI_SS
#define PIN_SPI_MOSI  PA7
#define PIN_SPI_MISO  PA6
#define PIN_SPI_SCK   PA5
#define PIN_SPI_SS    PA4 

#define W5500_CS PA4
#define W5500_RST PB0

#define LED_STATUS PC14
#define LED_LAN PC13
#define RF_CE   PA12
#define RF_CSN  PA11
#define RF_SPI_MOSI PB15
#define RF_SPI_MISO PB14
#define RF_SPI_SCK PB10

#define TX_LOG PA9
#define RX_LOG PA10
#define RF_SPI_IRQ PA8

// rf 
#define LAST_RF_TX_MS 1000
#define LAST_ROVER_MSG_MS 20
#define DELAY_CHUNK_MICRO 200

#define RF_DATA_RATE RF24_250KBPS // RF24_250KBPS, RF24_1MBPS, RF24_2MBPS
#define RF_PA RF24_PA_MAX // RF24_PA_MIN, RF24_PA_LOW, RF24_PA_HIGH, RF24_PA_MAX
#define RF_CHANNEL 80

#define RTCM_IDLE_GAP_MS 50
#define RF_RTCM_CHUNK_DATA_SIZE 25
#define RTCM_START_MSG_ID 1074
// uart
#define UART_RX_BUFFER_SIZE 4096
#define UDP_PAYLOAD_BUFF_SIZE 256
#define TX_STORAGE_SIZE 2048 
#define DMA_TX_TEMP_SIZE    1024  // Buffer tạm cho mỗi lần DMA bắn đi
#define SERIAL_LOG_BAUDRATE 115200

// timer
#define STATUS_TIMER TIM3
#define ROVER_TIMEOUT_MS 10000
#define SERVER_TIMEOUT_MS 20000

#define MAX_ROVER 5

// LAN
#define SERVER_PORT 192
// #define SERVER_PORT 8001
#define LOCAL_PORT 8888
#define LAST_ETH_SEND 20

// config um980
#define CONFIG_TIME_OUT_MS 500
#define CONFIG_START "CONFIG"
#define CONFIG_END "CHECK"
#define CONFIG_END "DONE"
#define CONFIG_OK "OK"
#define CONFIG_TIMEOUT 60000

#define WATCHDOG_TIME 10000000

#define CFG_QUEUE_MAX 10

const uint8_t CLIENT_ADDRESS[] = { 10, 2, 132, 179 };
// const uint8_t SERVER_ADDRESS[] = { 113, 160, 247, 168 };
const uint8_t SERVER_ADDRESS[] = { 10, 2, 132, 176 }; 
// const uint8_t SERVER_ADDRESS[] = { 192, 168, 1, 24 }; 

const uint8_t GATEWAY_ADDRESS[] = { 10, 2, 132, 129 };
const uint8_t SUBNET_ADDRESS[] =  { 255, 255, 255, 128 };
const uint8_t DNS_ADDRESS[] =  { 8, 8, 8, 8 };
const uint8_t LAN_MAC[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

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
  uint8_t device_id;   // ID của Rover
  uint8_t type;       // 0xB1 (Nhãn nhận diện)
  uint32_t time;      // Thời gian GPS (HHMMSS)
  int64_t lat;        // Vĩ độ nhân 10^7
  int64_t lon;        // Kinh độ nhân 10^7
  uint8_t battery;    // Phần trăm pin
  uint8_t modeRTK;    // Chế độ RTK (0, 1, 2, 4, 5...)
  uint8_t typeButton;
};

struct UDP_Command {
  uint8_t device_id;
  uint8_t type;
  uint8_t* data;
};

enum TYPE_Packet_Enum {
  TYPE_RTCM,
  TYPE_REPORT,
  TYPE_REPORT_REPEATED,
  TYPE_BUTTON_PRESSED,
  TYPE_NOTI,
  TYPE_MSG,
  TYPE_PING
};

enum STATUS_Enum {
  STATUS_DISCONNECT,
  STATUS_CONNECT,
};

enum CONFIG_STATE {
  CFG_IDLE,
  CFG_SEND_CMD,
  CFG_WAIT_RESP
};


#endif 