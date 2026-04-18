#include "uart_dma.h"

RF_Rover_Report current_report = { DEVICE_ID, TYPE_REPORT, 0, 0, 0, 0, 0, 0 };

HardwareSerial SerialLog(RX_LOG, TX_LOG);

SPIClass RF_SPI(RF_SPI_MOSI, RF_SPI_MISO, RF_SPI_SCK); // MOSI, MISO, SCK
RF24 radio(RF_CE, RF_CSN);

uint32_t last_rf_rx_ms = 0;
uint32_t last_report_ms = 0;
volatile bool rf_data_event = false;

volatile uint16_t rx_rtcm_count_in_s = 0;

HardwareTimer *StatusTimer;
volatile uint32_t system_ticks = 0;
volatile STATUS_Enum current_status = STATUS_DISCONNECT;

HardwareTimer *BuzzerTimer;
uint32_t buzzer_channel;
volatile int buzzer_beeps_count = 0;
volatile int buzzer_beeps_freq = BUZZER_NOTI_FREQ;
volatile int buzzer_tick_sub = 0;

volatile uint8_t button_pressed_limit = 0;
volatile bool button_pressed = false;
volatile uint32_t last_debounce_time = 0;
volatile uint32_t last_button_send_ms = 0;
volatile bool button_waiting_ack = false;

volatile uint8_t rtcm_fail_streak = 0;
uint32_t last_processed_tick = 999999; 

uint32_t last_check_battery = 0;

uint32_t current_base_time = 0;
uint8_t current_base = 0;

volatile uint32_t last_low_battery_beep = 0;

void setup() {
  // put your setup code here, to run once:
  SerialLog.begin(SERIAL_LOG_BAUDRATE);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  // blink led to wait booting
  for(int i=0; i < 20; i++) {
    delay(100);
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN)); 
  }

  RF_Init();
  UART_DMA_Init();
  Buzzer_Init();
  Timer_Init();
  Button_Init();
  Battery_Init();
  
  SerialLog.println("Inited!");
}

void loop() {
  
  check_battery();

  if (current_status != STATUS_LOW_BATTERRY) {
    trigger_button();

    process_rf_receive();
    
    check_rtcm_connection();

    process_gps_data();

    UART_DMA_Process_TX();

    send_report();
  }
  else
  {
    if (millis() - last_low_battery_beep > LAST_CHECK_BATTERY) {
      last_low_battery_beep = millis();
      trigger_buzzer(1, BUZZER_NOTI_FREQ);
    }
  }

  update_status_logic();

  // switch to default base
  if (millis() - last_rf_rx_ms > LAST_BASE_TIMEOUT)
  {
    current_base = 0;
  }

  // test buzzer
  // test_buzzer();
}

void check_rtcm_connection() {
  if ((system_ticks % 10 == 0) && (system_ticks != last_processed_tick)) {
    last_processed_tick = system_ticks;

    if (rx_rtcm_count_in_s > 0) {
      rtcm_fail_streak = 0;
    } 
    else {
      if (rtcm_fail_streak < RTCM_TIMEOUT_SEC) {
        rtcm_fail_streak++;
      }
    }

#ifdef DEBUG
    SerialLog.print("RTCM pkts: "); SerialLog.print(rx_rtcm_count_in_s);
    SerialLog.print(" | Fail streak: "); SerialLog.print(rtcm_fail_streak);
    SerialLog.print(", at base: "); SerialLog.println(current_base);
#endif

    rx_rtcm_count_in_s = 0;
  }
}

void update_status_logic() {
  if (current_report.battery < LOW_BATTERY_THRESHOLD) { 
    current_status = STATUS_LOW_BATTERRY;
  }
  else if (rtcm_fail_streak >= RTCM_TIMEOUT_SEC) {
    current_status = STATUS_DISCONNECT;
  }
  else {
    if (current_report.modeRTK == RTK_MODE_FIXED) { // Fixed
      current_status = STATUS_MODE_FIXED;
    } 
    else if (current_report.modeRTK > RTK_MODE_INVALID) { // Float or 3D Fix
      current_status = STATUS_MODE_FLOAT;
    } 
    else {
      current_status = STATUS_CONNECT;
    }
  }
}

void send_report() {
  uint32_t now = millis();
  uint32_t current_cycle_tick = system_ticks % CYCLE_TICKS;
  uint32_t my_slot_tick = (DEVICE_ID - 1) * SLOT_STEP_TICKS;

  uint32_t max_last_report_ms = button_waiting_ack ? uint32_t(LAST_REPORT_MS / 5) : LAST_REPORT_MS;

  if (current_cycle_tick == my_slot_tick || button_waiting_ack) {
    if ((now - last_report_ms >= max_last_report_ms) && 
        (now - last_rf_rx_ms >= LAST_RF_RX_MS)) 
    {
      last_report_ms = now;
      send_to_base(button_waiting_ack);
#ifdef DEBUG
      SerialLog.print("ID "); SerialLog.print(DEVICE_ID);
      SerialLog.print(" sent at tick "); SerialLog.println(current_cycle_tick);
#endif
    }
  }
}

// test buzzer
volatile uint32_t time_freq = 0;
volatile uint32_t freq = 0;
void test_buzzer() {
  if (millis() - time_freq > 3000)
  {
    trigger_buzzer(10, freq);
    SerialLog.println(freq);
    time_freq = millis();
    if (freq == 20000)
      freq = 0;
    freq += 500;
  }
}

void RF_Init() {
  #ifdef USBCON
    USBDevice.detach();
  #endif

  pinMode(RF_CE, OUTPUT);
  digitalWrite(RF_CE, LOW);
  pinMode(RF_CSN, OUTPUT);
  digitalWrite(RF_CSN, HIGH);

  pinMode(RF_SPI_IRQ, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(RF_SPI_IRQ), check_rf_irq, FALLING);

  delay(100);

  RF_SPI.begin(); 

  if (!radio.begin(&RF_SPI))
  {
    SerialLog.println("RF start failed..");
  }
  radio.setDataRate(RF_DATA_RATE);
  radio.setPALevel(RF_PA);
  radio.setChannel(RF_CHANNEL);
  radio.setAutoAck(false); // Broadcast
  radio.setRetries(0, 0);
  radio.maskIRQ(true, true, false);

  radio.openReadingPipe(1, ADDR_BASE_TO_ALL);
  radio.openWritingPipe(ADDR_ROVER_TO_BASE);

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
    rf_data_event = false;
    bool tx_ok, tx_fail, rx_ready;
    radio.whatHappened(tx_ok, tx_fail, rx_ready);

    if (rx_ready) {
      uint8_t limit = 0;
      RF_RTCM_Chunk pkt;
      while (radio.available() && limit < 5) {
        radio.read(&pkt, sizeof(pkt));
        if (pkt.device_id == 0 || pkt.device_id == DEVICE_ID || pkt.device_id == 99) // 0 and 99 is main base and preventive base
          handle_rf_rx(pkt);
        limit++;
      }                                                                                                                    
      last_rf_rx_ms = millis();
    }

    if (tx_fail) { radio.flush_tx(); }

    if (tx_ok) {}
  }
}

void handle_rtcm(uint8_t* data) {
  UART_Write_Queue(data, RF_RTCM_CHUNK_DATA_SIZE);
  rx_rtcm_count_in_s++;
}

void handle_button_response() {
  if (button_waiting_ack) {
    button_waiting_ack = false; 
    button_pressed_limit = 0;
#ifdef DEBUG
    SerialLog.println("Base Received! Stopping retransmission.");
#endif
  }
}

void handle_notify() {
  trigger_buzzer(15, BUZZER_NOTI_FREQ); 
}

void handle_rf_rx(RF_RTCM_Chunk &pkt)
{
#ifdef DEBUG
  SerialLog.print("rf-type received: "); SerialLog.print(pkt.type); 
  SerialLog.print("batch_id="); SerialLog.print(pkt.batch_id);
  SerialLog.print(", seq="); SerialLog.println(pkt.seq);
#endif

  switch(pkt.type)
  {
    case TYPE_RTCM:
      if (pkt.seq == 0) {
        if (millis() - current_base_time > LAST_BASE_TIMEOUT / 2) {
          current_base_time = millis();
          current_base = pkt.device_id;
        }
      }

      if (pkt.device_id == current_base)
      {
        handle_rtcm(pkt.data);
      }
      break;
    case TYPE_NOTI: handle_notify(); break;
    case TYPE_BUTTON_PRESSED: handle_button_response(); break;
    default: break;
  }
}

void send_to_base(bool is_button) {
  radio.stopListening();
  
  current_report.typeButton = is_button ? BTN_STT_PRESSED : BTN_STT_RELEASE; 

  // test log preventive base
  // current_report.typeButton = current_base;

  bool success = radio.write(&current_report, sizeof(RF_Rover_Report));


#ifdef DEBUG
  SerialLog.print("rf-type sent: "); SerialLog.print(current_report.type);
  if (success) { SerialLog.println(", report ok"); }
  else {
    radio.flush_tx(); 
    SerialLog.println(", report fail");
  }
#endif

  radio.startListening();
}

void process_gps_data() {
  static uint8_t temp_buffer[UART_RX_BUFFER_SIZE];
  static char line_buffer[128];
  static int idx = 0;

  uint16_t received_len = 0;
  if (uart_idle_flag) {
    uart_idle_flag = false;
    
    received_len = UART_Read(temp_buffer, sizeof(temp_buffer));
  }

  if (received_len > 0) {
#ifdef DEBUG
    SerialLog.print("NMEA="); SerialLog.println(received_len); 
#endif
    // browse through the received array to find the line GGA
    for (uint16_t i = 0; i < received_len; i++) {
      char c = (char)temp_buffer[i];

      if (c == '\n' || c == '\r') {
        if (idx > 10) {
          line_buffer[idx] = '\0';
          parse_gga(line_buffer);
        }
        idx = 0;
      } else if (idx < sizeof(line_buffer) - 1) {
        line_buffer[idx++] = c;
      }
    }
  }
}

void parse_gga(char *nmeastr) {
  if (strstr(nmeastr, "GGA") == NULL) return;

  int field = 0;
  char *p = nmeastr;
  char *token;
  double alt = 0, geoid = 0;
  uint8_t satUsed = 0;
  char ns = 'N', ew = 'E';  // direcion

  while ((token = strsep(&p, ",")) != NULL) {

    if (field == 1 && strlen(token) > 0) {  // Time
      current_report.time = atol(token);
    } else if (field == 2 && strlen(token) > 0) {  // Latitude raw
      double raw_lat = atof(token);
      int degrees = (int)(raw_lat / 100);
      double minutes = raw_lat - (degrees * 100);
      double lat = degrees + (minutes / 60.0);
      current_report.lat = (int64_t)(lat * 1e10);
    } else if (field == 3 && strlen(token) > 0) {  // N/S
      ns = token[0];
    } else if (field == 4 && strlen(token) > 0) {  // Longitude raw
      double raw_lon = atof(token);
      int degrees = (int)(raw_lon / 100);
      double minutes = raw_lon - (degrees * 100);
      double lon = degrees + (minutes / 60.0);
      current_report.lon = (int64_t)(lon * 1e10);
    } else if (field == 5 && strlen(token) > 0) {  // E/W
      ew = token[0];
    } else if (field == 6 && strlen(token) > 0) {  // RTK mode
      current_report.modeRTK = atoi(token);
    } else if (field == 7 && strlen(token) > 0) {  // satellites
      satUsed = atoi(token);
    } else if (field == 9 && strlen(token) > 0) {  // altitude MSL
      alt = atof(token);
    } else if (field == 11 && strlen(token) > 0) { // geoid
      geoid = atof(token);
    }
    field++;
  }

  if (ns == 'S') current_report.lat = -current_report.lat;
  if (ew == 'W') current_report.lon = -current_report.lon;

#ifdef DEBUG
  SerialLog.print("Mode="); SerialLog.print(current_report.modeRTK);
  SerialLog.print(", lat="); SerialLog.print(current_report.lat);
  SerialLog.print(", lon="); SerialLog.print(current_report.lon);
  SerialLog.print(", satUsed="); SerialLog.print(satUsed);
  SerialLog.print(", h="); SerialLog.println(alt); 
#endif
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
  update_buzzer();
}

void update_buzzer() {
  if (buzzer_beeps_count > 0) {
    buzzer_tick_sub++; 
    
    if (buzzer_tick_sub % 2 != 0) {
      digitalWrite(BUZZER_PIN, HIGH); // odd: beep
    } else {
      digitalWrite(BUZZER_PIN, LOW);  // even: off
      buzzer_beeps_count--;           // done beep cycle
    }
  } else {
    digitalWrite(BUZZER_PIN, LOW);
    buzzer_tick_sub = 0; // reset beep count
  }
}

void update_led() {
  switch (current_status) {
    case STATUS_DISCONNECT:
      digitalWrite(LED_BUILTIN, HIGH); break;
    case STATUS_LOW_BATTERRY: // fast blink (200ms)
      if (system_ticks % 2 == 0) digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN)); break;
    case STATUS_MODE_FLOAT: // slow blink (1s)
      if (system_ticks % 10 == 0) digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN)); break;
    case STATUS_MODE_FIXED:
      digitalWrite(LED_BUILTIN, LOW); break;
    default:
      break;
  }
}

void Buzzer_Init(){
  // use IO
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW); 

  // use PWM
  // PinName p = digitalPinToPinName(BUZZER_PIN);
  // buzzer_channel = STM_PIN_CHANNEL(pinmap_function(p, PinMap_PWM));
  // BuzzerTimer = new HardwareTimer(BUZZER_TIMER); 
  // BuzzerTimer->pause();
  // BuzzerTimer->setMode(buzzer_channel, TIMER_OUTPUT_COMPARE_PWM1, BUZZER_PIN);
  // BuzzerTimer->setCaptureCompare(buzzer_channel, 50, PERCENT_COMPARE_FORMAT); 
  // HAL_NVIC_SetPriority(TIM4_IRQn, 15, 0); 
  // BuzzerTimer->pause();
}

void trigger_buzzer(int beeps, int freq) {
  noInterrupts();
  buzzer_beeps_count = beeps;
  buzzer_beeps_freq = freq;
  interrupts();
}

void Button_Init() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  // use interrupt with != EXIT8
  // attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), button_callback, FALLING);
}

// void button_callback() {  
//   uint32_t now = millis();
//   if (now - last_debounce_time > MAX_DEBOUNCE_TIME) {
//     if (!button_waiting_ack) { 
//       button_pressed = true;
//       button_pressed_limit = MAX_BUTTON_RETRY; 
//       button_waiting_ack = true;
//       trigger_buzzer(1, BUZZER_NOTI_FREQ);  
//       digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
//     }
//     last_debounce_time = now;  
//   }
// }

void trigger_button() {
  uint32_t now = millis();
  bool current_button_state = (digitalRead(BUTTON_PIN) == LOW); 
  if (current_button_state) {
    if (now - last_debounce_time > MAX_DEBOUNCE_TIME) {
      if (!button_waiting_ack) {
        button_pressed = true;
        button_pressed_limit = MAX_BUTTON_RETRY;
        button_waiting_ack = true;
        last_button_send_ms = 0;

        trigger_buzzer(1, BUZZER_NOTI_FREQ);
        noInterrupts();
        buzzer_tick_sub = 0; 
        interrupts();
#ifdef DEBUG
        SerialLog.println("Button Clicked (Polling)");
#endif
      }
      last_debounce_time = now;
    }
  }

  if (button_waiting_ack && button_pressed_limit > 0) {
    if (now - last_button_send_ms > BUTTON_RETRY_INTERVAL) {
      last_button_send_ms = now;
#ifdef DEBUG
      SerialLog.print("Sending Button ID to Base, Retry left: "); SerialLog.println(button_pressed_limit);
#endif
      // send_to_base(true);
      button_pressed_limit--;
      
      if (button_pressed_limit == 0) {
        button_waiting_ack = false;
#ifdef DEBUG
        SerialLog.println("No feedback from Base. Giving up.");
#endif
      }
    }
  }
}

void Battery_Init() {
  Wire.setSCL(SCL_PIN);
  Wire.setSDA(SDA_PIN);
  Wire.begin();

  Wire.beginTransmission(MAX17048_ADDR);
  Wire.write(0xFE);   
  Wire.write(0x54);
  Wire.write(0x00);
  Wire.endTransmission();
  delay(100);

  Wire.beginTransmission(MAX17048_ADDR);
  Wire.write(0x06);   
  Wire.write(0x40);
  Wire.write(0x00);
  Wire.endTransmission();

  delay(200);
}

void check_battery() {
  uint32_t now = millis();
  if (now - last_check_battery >= LAST_CHECK_BATTERY) {
    last_check_battery = now;

    uint16_t vcell_raw = read16(0x02);
    uint16_t soc_raw = read16(0x04);

    float voltage = vcell_raw * 78.125e-6;
    float soc = soc_raw / 256.0;

#ifdef DEBUG
    SerialLog.print("voltage: "); SerialLog.print(voltage); 
    SerialLog.print(", soc: "); SerialLog.println(soc); 
#endif

    current_report.battery = soc > 100 ? 100 : soc;
  }
}

uint16_t read16(uint8_t reg) {
  Wire.beginTransmission(MAX17048_ADDR);
  Wire.write(reg);

  if (Wire.endTransmission(false) != 0) {
#ifdef DEBUG
    SerialLog.println("I2C TX FAIL");
#endif
    return 0xFFFF; // báo lỗi
  }

  uint8_t bytes = Wire.requestFrom(MAX17048_ADDR, (uint8_t)2);

  if (bytes != 2) {
#ifdef DEBUG
    SerialLog.println("I2C RX FAIL");
#endif
    return 0xFFFF;
  }

  uint8_t msb = Wire.read();
  uint8_t lsb = Wire.read();

  return (msb << 8) | lsb;
}
