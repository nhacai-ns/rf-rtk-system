// GENERIC BOARD

#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <string.h>

#define LED_BUILTIN PC13
SPIClass spi2(PB15, PB14, PB13); // MOSI, MISO, SCK
RF24 pRadio(PB2, PB10); // CE, CSN

// SPIClass spi2(PB15, PB14, PB10); // MOSI, MISO, SCK
// RF24 pRadio(PA12, PA11); // CE, CSN

const byte BASE_ADDR[6] = "12345";0
const byte ROVER_ADDR[6] = "54321";

volatile uint8_t flags = 0;

void setup() {

  pinMode(PB10, OUTPUT);
  digitalWrite(PB10, HIGH);
  pinMode(LED_BUILTIN, OUTPUT);
  // put your setup code here, to run once:
  Serial.begin(115200);

  for(int i=0; i<20; i++) {
    delay(100);
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN)); // Nháy led báo đang đợi nguồn
  }

  if (!pRadio.begin(&spi2)) {
      Serial.println("Init RF24 Failed");
  }

  pRadio.openWritingPipe(0xAABBCCDD11LL);
  pRadio.openReadingPipe(1, 0xAABBCCDD21LL);
  pRadio.setChannel(124);
  pRadio.setDataRate(RF24_250KBPS);
  pRadio.setPALevel(RF24_PA_MIN);
  pRadio.startListening();

  if (!pRadio.available())
  {
    Serial.println("Don't connect to RX...!!");
    Serial.println("Waiting.......");
  }

  delay(1000);
  pRadio.printDetails();
}
void loop() {
  // put your main code here, to run repeatedly:

}
