// generic board

#include <SPI.h>
#include <RF24.h>
#include <nRF24L01.h>
#include <string.h>

#define BASE

// sma board
#define LED_BUILTIN PC13
#define RF_CE   PB2
#define RF_CSN  PB10
#define RF_SPI_MOSI PB15
#define RF_SPI_MISO PB14
#define RF_SPI_SCK PB13

// ipx board
// #define LED_BUILTIN PC13
// #define RF_CE   PA12
// #define RF_CSN  PA11
// #define RF_SPI_MOSI PB15
// #define RF_SPI_MISO PB14
// #define RF_SPI_SCK PB10

// pcb board
// #define LED_BUILTIN PC13
// #define RF_CE   PB12
// #define RF_CSN  PB13
// #define RF_SPI_MOSI PB15
// #define RF_SPI_MISO PB14
// #define RF_SPI_SCK PB10

#define PACKET_TOTAL 30
#define RF_RECEIVE_SIZE 30

struct PACKET
{
  uint8_t batch = 0;
  uint8_t data[RF_RECEIVE_SIZE];
};
PACKET packet;

const uint64_t ADDR_BASE_TO_ALL = 0xAABBCCDD11LL;

#ifdef BASE
const uint64_t ADDR_ROVER_TO_BASE[] = {
  0xAABBCCDD21LL,
  0xAABBCCDD22LL,
  0xAABBCCDD23LL,
  0xAABBCCDD24LL,
};
uint8_t PACKET_SEND = PACKET_TOTAL;
uint8_t PACKET_REC = 1;
#else
const uint64_t ADDR_ROVER_TO_BASE = 0xAABBCCDD21LL;
uint8_t PACKET_SEND = 1;
uint8_t PACKET_REC = PACKET_TOTAL;
#endif

HardwareSerial SerialLog(PA10, PA9);

SPIClass RF_SPI(RF_SPI_MOSI, RF_SPI_MISO, RF_SPI_SCK); // MOSI, MISO, SCK
RF24 radio(RF_CE, RF_CSN);

unsigned long last_rtcm_rx_ms = 0;
unsigned long last_report_ms = 0;

void setup() {
  // put your setup code here, to run once:
  SerialLog.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);

  RF_Init();

  for(uint8_t i = 0; i < RF_RECEIVE_SIZE; i++)
  {
    packet.data[i] = i;
  }

  SerialLog.println("Inited!");
}

void loop() {
  // put your main code here, to run repeatedly:
  if (radio.available()) {
    PACKET packet_rec;
    radio.read(&packet_rec, sizeof(packet_rec));
    SerialLog.write(packet.data, sizeof(packet.data));
    last_rtcm_rx_ms = millis();

    if (packet_rec.batch == PACKET_REC - 1)
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  }

  if (millis() - last_report_ms >= 1000 && millis() - last_rtcm_rx_ms > 200) { 
    last_report_ms = millis();
    send();
  }
}

void send()
{
  radio.stopListening();

  bool res = false;
  for(uint8_t i = 0; i < PACKET_SEND; i++)
  {
    packet.batch = i;
    if(!radio.write(&packet, sizeof(packet))){
      radio.flush_tx();
      res = false;
    }
    else 
    {
      res = true;
    }
    delayMicroseconds(50);
  }
  radio.startListening();
}

void RF_Init() {
  if (!radio.begin(&RF_SPI))
  {
    SerialLog.println("Module cannot start..");
  }
  radio.setDataRate(RF24_2MBPS); // RF24_250KBPS, RF24_1MBPS, RF24_2MBPS
  radio.setPALevel(RF24_PA_MIN);
  radio.setChannel(124);

#ifdef BASE
  radio.openWritingPipe(ADDR_BASE_TO_ALL);
  for (int i = 0; i < 4; i++) {
    radio.openReadingPipe(i + 1, ADDR_ROVER_TO_BASE[i]);
  }
#else 
  radio.openReadingPipe(1, ADDR_BASE_TO_ALL);
  radio.openWritingPipe(ADDR_ROVER_TO_BASE);
#endif

  if (!radio.available())
  {
    SerialLog.println("Don't connect to RX...!!");
    SerialLog.println("Waiting.......");
  }

  radio.startListening();
}