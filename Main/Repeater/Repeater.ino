#include "configs.h"

HardwareSerial SerialLog(RX_LOG, TX_LOG);

SPIClass RF_SPI(RF_SPI_MOSI, RF_SPI_MISO, RF_SPI_SCK); // MOSI, MISO, SCK
RF24 radio(RF_CE, RF_CSN);

volatile bool rf_data_event = false;

HardwareTimer *StatusTimer;
volatile uint32_t system_ticks = 0;

volatile STATUS_Enum current_status = STATUS_DISCONNECT;

uint32_t last_check_battery = 0;
uint32_t last_ping = 0;

uint32_t last_rover_msg_ms = 0;

volatile bool loop_ok = false; 
volatile bool rf_ok = false; 

void setup() {
  // put your setup code here, to run once:
  SerialLog.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  for(int i=0; i<20; i++) {
    delay(100);
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  }

  RF_Init();
  Timer_Init();

  IWatchdog.begin(5000000); // 5s timeout

  SerialLog.println("Repeater Inited!");
}


void check_watch_dog(){
  static uint32_t lastCheck = 0;

  if (millis() - lastCheck < 1000) return;
  lastCheck = millis();

  if (loop_ok && rf_ok) {
    IWatchdog.reload(); // feed watchdog
  }

  loop_ok = false;
  rf_ok = false;
}

void loop() {

  loop_ok = true;

  process_rf_receive();

  check_rover_connection();

  if (current_status == STATUS_DISCONNECT) {
    update_ping();
  }
  
  check_watch_dog();
}

void RF_Init() {
  #ifdef USBCON
    USBDevice.detach();
  #endif
  pinMode(RF_CE, OUTPUT); // CE
  digitalWrite(RF_CE, LOW);
  pinMode(RF_CSN, OUTPUT); // CSN
  digitalWrite(RF_CSN, HIGH);

  pinMode(RF_SPI_IRQ, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(RF_SPI_IRQ), check_rf_irq, FALLING);

  delay(100);

  RF_SPI.begin(); 

  if (!radio.begin(&RF_SPI))
  {
    SerialLog.println("RF start fail..");
  }
  radio.setDataRate(RF_DATA_RATE);
  radio.setPALevel(RF_PA);
  radio.setChannel(RF_CHANNEL);
  radio.setAutoAck(false);

  radio.openReadingPipe(1, ADDR_REPEATER);
  radio.openWritingPipe(ADDR_REPEATER);

  radio.startListening();

  SerialLog.println("Done.");
}

void check_rf_irq()
{
  rf_data_event = true;
}

void process_rf_receive()
{
  if (rf_data_event) {
    rf_data_event = false; // Reset cờ phần mềm
    bool tx_ok, tx_fail, rx_ready;
    radio.whatHappened(tx_ok, tx_fail, rx_ready);

    // 1. Nếu có dữ liệu đến (RX_DR)
    if (rx_ready) {
#ifdef BASE_REPEATER
      RF_RTCM_Chunk rpt;
#else
      RF_Rover_Report rpt;
#endif

      uint8_t limit = 0;
      while (radio.available() && limit < 5) {
        radio.read(&rpt, sizeof(rpt));
#ifdef BASE_REPEATER
        if (rpt.type == TYPE_RTCM) {
          rpt.type = TYPE_RTCM_REPEATED; 
#else
        if (rpt.type == TYPE_REPORT) {
          rpt.type = TYPE_REPORT_REPEATED;
          rpt.repeater_id = REPEATER_ID;
#endif 
          radio.stopListening();
          radio.write(&rpt, sizeof(rpt));
          radio.startListening();

#ifdef BASE_REPEATER
          SerialLog.print(", batch_id: ");
          SerialLog.print(rpt.batch_id);
          SerialLog.print(", seq: ");
          SerialLog.print(rpt.seq);
#else
          SerialLog.print("Repeated Rover ID: ");
          SerialLog.print(rpt.device_id);
#endif
          SerialLog.println();
        }
        limit++;
      }
      last_rover_msg_ms = millis();                                                                                                     
    }

    if (tx_fail) { radio.flush_tx(); }

    if (tx_ok) {}
  }
  rf_ok = true;
}

void Timer_Init()
{
  StatusTimer = new HardwareTimer(STATUS_TIMER);
  StatusTimer->pause();
  StatusTimer->setOverflow(10, HERTZ_FORMAT);

  HAL_NVIC_SetPriority(TIM3_IRQn, 14, 0); 

  StatusTimer->attachInterrupt(timer_callback);
  StatusTimer->resume();
}

void timer_callback(){
  system_ticks++;
  
  update_led();
}

void update_led() {
  switch (current_status) {
    case STATUS_DISCONNECT:
      digitalWrite(LED_BUILTIN, HIGH); break;
    case STATUS_CONNECT:
      digitalWrite(LED_BUILTIN, LOW); break;
    default:
      break;
  }
}

void check_rover_connection() {
  if (millis() - last_rover_msg_ms > 5000) {
    current_status = STATUS_DISCONNECT;
  }
  else
  {
    current_status = STATUS_CONNECT;
  }
}

void update_ping() {
  if (millis() - last_ping > 1000)
  {
    last_ping = millis();
    RF_Rover_Report rrr;
    rrr.device_id = 0;
    rrr.type = TYPE_REPORT_REPEATED;
    rrr.repeater_id = REPEATER_ID;
    radio.stopListening();
    radio.write(&rrr, sizeof(rrr));
    radio.startListening();
  }
}
