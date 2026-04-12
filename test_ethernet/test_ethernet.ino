#include <SPI.h>
#include <string.h>


#define ETH_SPI_MOSI PA7
#define ETH_SPI_MISO PA6
#define ETH_SPI_SCK PA5
#define W5500_CS PA4

HardwareSerial SerialLog(PA10, PA9);

uint8_t readW5500Version() {
  digitalWrite(W5500_CS, LOW);
  // Khung truyền W5500: [Address High] [Address Low] [Control Byte] [Data]
  SPI.transfer(0x00);      // Address High (0x00)
  SPI.transfer(0x39);      // Address Low (0x39 - VERSIONR)
  SPI.transfer(0x00);      // Control Byte (0x00 = Read, Common Register)
  
  uint8_t version = SPI.transfer(0x00); // Nhận dữ liệu trả về
  
  digitalWrite(W5500_CS, HIGH);
  return version;
}

void setup() {
  SerialLog.begin(115200);
  pinMode(W5500_CS, OUTPUT);
  digitalWrite(W5500_CS, HIGH);

  SPI.setMOSI(ETH_SPI_MOSI);
  SPI.setMISO(ETH_SPI_MISO);
  SPI.setSCLK(ETH_SPI_SCK);

  SPI.begin();
  
  // Hạ tốc độ SPI xuống 4MHz để kiểm tra độ ổn định bit ban đầu
  SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));

  uint8_t ver = readW5500Version();
  SerialLog.print("Chip Version: 0x");
  SerialLog.println(ver, HEX);

  if (ver == 0x04) {
    SerialLog.println("KẾT NỐI SPI THÀNH CÔNG (Bit-perfect)");
  } else {
    SerialLog.println("LỖI KẾT NỐI: Kiểm tra lại dây dẫn hoặc nguồn");
  }
}

void loop() {


}
