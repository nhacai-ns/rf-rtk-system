// generic board

#include "rtcm_parser.h"
#include "uart_dma.h"

HardwareSerial SerialLog(RX_LOG, TX_LOG);

SPIClass RF_SPI(RF_SPI_MOSI, RF_SPI_MISO, RF_SPI_SCK); // MOSI, MISO, SCK
RF24 radio(RF_CE, RF_CSN);

volatile uint32_t last_rf_tx_ms = 0;

uint8_t rtcm_data[UART_RX_BUFFER_SIZE];
uint8_t buff_data[UART_RX_BUFFER_SIZE];
bool cycle_done = false;
uint16_t rtcm_length = 0;
uint8_t batch_id;
uint32_t start_time = 0;

IPAddress client_address(CLIENT_ADDRESS), server_address(SERVER_ADDRESS), gateway_address(GATEWAY_ADDRESS), 
  subnet_address(SUBNET_ADDRESS), dns_address(DNS_ADDRESS);
EthernetUDP Udp;
uint8_t udpSock = 0;
volatile uint32_t last_udp_healthy = 0;
volatile uint32_t last_udp_config = 0;
volatile bool lan_ready_flag = false;
volatile uint32_t last_eth_report_send = 0;

volatile bool rf_data_event = false;

volatile STATUS_Enum server_status = STATUS_CONNECT;
volatile STATUS_Enum rover_status = STATUS_CONNECT;
volatile uint32_t last_rover_msg_ms = 0;
volatile uint32_t last_server_ping_ms = 0;
volatile bool clear_rf_flag = false;

HardwareTimer *StatusTimer;
volatile uint32_t system_ticks = 0;

RF_Rover_Report ROVER_LIST[MAX_ROVER] = {0};
int8_t ROVER_MODE[MAX_ROVER] = { -1 };
uint8_t REPEATER_LIST[MAX_REPEATER] = { 0 };
uint32_t REPEATER_LAST_SEEN[MAX_REPEATER] = {0};

bool is_in_config_mode = false; 

volatile bool loop_ok = false, rf_ok = false, udp_ok = false;

CONFIG_STATE cfg_state = CFG_IDLE;

String cfg_cmd = "";
uint32_t cfg_start_time = 0;
std::deque<String> cfg_queue;
String cfg_response = "";
volatile bool cfg_force_exit = false;

void setup() {
  SerialLog.begin(SERIAL_LOG_BAUDRATE);
  SerialLog.println("System Booting...");

  ROVERS_INIT();  
  LED_INIT();
  UART_DMA_Init();
  TIMER_INIT();
  RF_Init();
#ifdef LAN
  LAN_Init();
#endif

  IWatchdog.begin(WATCHDOG_TIME); // 10s timeout

  SerialLog.println("System Ready!");
}

void loop() {

  loop_ok = true;

  // hoạt động ở chế độ cấu hình qua cổng serial debug
  check_serial_config_mode();

  if (!is_in_config_mode)
  {
    /* hoạt động ở chế độ normal, lần lượt như sau:
      1: nhận polling rf (thông tin rover)
      2: lấy và ghép nối rtcm từ dma uart
      3: xử lý việc gửi rtcm ở trong bộ đệm ring buffer
      4: gửi dữ liệu đến udp server
      5: kiểm tra các connection bao gồm rover và tín hiệu ping từ server
      6: cơ chế tự flush fifo của rf và re-init rf khi bị nghẽn cờ ngắt irq 
    */
    process_rf_receiver();

    get_and_aggregate_rtcm();

    process_rtcm_sending();

    send_report_to_server();

    check_connection_status();
  
    rf_auto_recover();
  }
  else {
    // hoạt động ở chế độ cấu hình qua LAN
    process_lan_config_mode();
  }

  // handle dữ liệu được trả về từ server
#ifdef LAN
  handle_udp();
#endif
  // cờ reset watch của task rf
  rf_ok = radio.isChipConnected();
  
  // kiểm tra và reload watchdog, nếu không sẽ đếm timeout để reset
  check_watch_dog();
}

void ROVERS_INIT() {
  for(uint8_t id = 0; id < MAX_ROVER; id++) {
    ROVER_LIST[id].device_id = id + 1;
  }
}

void LED_INIT()
{
  pinMode(LED_STATUS, OUTPUT);
  pinMode(LED_LAN, OUTPUT);
  digitalWrite(LED_STATUS, LOW);
  digitalWrite(LED_LAN, LOW);

  for(int i=0; i<20; i++) {
    delay(100);
    digitalWrite(LED_STATUS, !digitalRead(LED_STATUS));
  }
}

void RF_Init() {

  SerialLog.println("Init RF24...");

  pinMode(RF_CE, OUTPUT);
  digitalWrite(RF_CE, LOW);
  pinMode(RF_CSN, OUTPUT);
  digitalWrite(RF_CSN, HIGH);

  pinMode(RF_SPI_IRQ, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(RF_SPI_IRQ), check_rf_irq, FALLING);

  RF_SPI.begin();
  if (!radio.begin(&RF_SPI))
  {
    SerialLog.println("RF start fail..");
  }
  radio.setDataRate(RF_DATA_RATE);
  radio.setPALevel(RF_PA);
  radio.setChannel(RF_CHANNEL);
  radio.setAutoAck(false);
  radio.setRetries(0, 0);
  radio.maskIRQ(true, true, false);

  radio.openWritingPipe(ADDR_BASE_TO_ALL);
  radio.openReadingPipe(1, ADDR_ROVER_TO_BASE);

  radio.startListening();

  SerialLog.println("Done.");
}


void LAN_Init() {
  SerialLog.println("Init LAN...");

  pinMode(W5500_RST, OUTPUT);
  pinMode(W5500_CS, OUTPUT);

  digitalWrite(W5500_CS, HIGH);
  digitalWrite(W5500_RST, LOW); delay(100);
  digitalWrite(W5500_RST, HIGH); delay(100);

  SPI.setMOSI(PIN_SPI_MOSI);
  SPI.setMISO(PIN_SPI_MISO);
  SPI.setSCLK(PIN_SPI_SCK);
  SPI.begin();
  SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));

  digitalWrite(LED_LAN, HIGH);

  uint8_t ver = readW5500Version();

  SerialLog.print("Chip Version: 0x");
  SerialLog.println(ver, HEX);
  if (ver != W5500_VER) {
    return;
  }

  Ethernet.init(W5500_CS);

  Ethernet.begin((uint8_t*)LAN_MAC, client_address, dns_address, gateway_address, subnet_address);

  if (Udp.begin(LOCAL_PORT)) {
    SerialLog.print("UDP Initialized. Local IP: ");
    SerialLog.println(Ethernet.localIP());
    lan_ready_flag = true;
    digitalWrite(LED_LAN, LOW);
  } else {
    SerialLog.println("Failed to begin UDP.");
  }

  SerialLog.println("Done.");
}

uint8_t readW5500Version() {
  digitalWrite(W5500_CS, LOW);
  SPI.transfer(0x00);      // Address High (0x00)
  SPI.transfer(0x39);      // Address Low (0x39 - VERSIONR)
  SPI.transfer(0x00);      // Control Byte (0x00 = Read, Common Register)

  uint8_t version = SPI.transfer(0x00);

  digitalWrite(W5500_CS, HIGH);
  return version;
}

void check_serial_config_mode() {
  if (SerialLog.available()) {
    String input = SerialLog.readStringUntil('\n');
    input.trim();

    if (input == CONFIG_START) {
      SerialLog.println("\n--- CONFIG MODE, input 'DONE' to exit ---");
      uint32_t last_serial_config = millis();

      while (true || millis() - last_serial_config > CONFIG_TIMEOUT) {

        loop_ok = rf_ok = udp_ok = true;
        check_watch_dog();

        UART_DMA_Process_TX(); 

        // Gửi lệnh từ PC -> UM980
        if (SerialLog.available()) {
          String cmd = SerialLog.readStringUntil('\n');
          cmd.trim();
          last_serial_config = millis();

          if (cmd == CONFIG_END) break;

          SerialLog.print(cmd);
          UART_Write_Queue((uint8_t*)cmd.c_str(), cmd.length());
          UART_Write_Queue((uint8_t*)"\r\n", 2);

          /// 3. Đợi phản hồi
          uint32_t start_time = millis();
          bool is_ok = false;
          const char* p_match = CONFIG_OK; // Con trỏ theo dõi chuỗi cần tìm

          while (millis() - start_time < CONFIG_TIME_OUT_MS) {
            UART_DMA_Process_TX(); // Duy trì gửi dữ liệu

            uint8_t c;
            if (UART_Read(&c, 1) > 0) { // Đọc từng byte một
              // Thuật toán so khớp chuỗi từng ký tự (giúp bỏ qua rác binary)
              if (c == *p_match) {
                p_match++;
                if (*p_match == '\0') { // Đã khớp toàn bộ chuỗi (ví dụ: "OK")
                  is_ok = true;
                  break;
                }
              } else {
                p_match = CONFIG_OK; // Nếu sai, quay lại tìm từ đầu
              }
            }
            yield();
          }
          if (is_ok) { SerialLog.println(" -> OK."); } 
          else { SerialLog.println(" -> FAIL - TIMEOUT."); }
        }
      }
      SerialLog.println("--- EXIT CONFIG ---");
    }
  }
}

void check_watch_dog(){
  static uint32_t lastCheck = 0;

  if (millis() - lastCheck < 500) return;
  lastCheck = millis();

  if (loop_ok && rf_ok) {
    IWatchdog.reload(); // feed watchdog
  }

  loop_ok = false;
  rf_ok = false;
}

void check_rf_irq()
{
  rf_data_event = true;
}

void process_rf_receiver()
{
  if (!rf_data_event) return;
  rf_data_event = false;

  bool tx_ok, tx_fail, rx_ready;
  radio.whatHappened(tx_ok, tx_fail, rx_ready);

  if (rx_ready) {

    RF_Rover_Report temp_rpt;
    uint8_t safety = 0;
#ifdef DEBUG
    SerialLog.println("⚠ Rx Ready");  
#endif

    while (radio.available()) {

      radio.read(&temp_rpt, sizeof(temp_rpt));
      safety++;

#ifdef DEBUG_
      SerialLog.print("type: ");
      SerialLog.print(temp_rpt.type);
      SerialLog.print("packet_id: ");
      SerialLog.print(temp_rpt.battery);
      SerialLog.print(", repeater id: ");
      SerialLog.println(temp_rpt.repeater_id);
#endif

      if (temp_rpt.type == TYPE_REPORT_REPEATED) {

        uint8_t repeater_id = temp_rpt.repeater_id - 1;
        REPEATER_LIST[repeater_id] = 1;
        REPEATER_LAST_SEEN[repeater_id] = millis();

        // 👉 repeater ping
        if (temp_rpt.device_id == 0) {
          continue;
        }
      }

      // 👉 button
      if (temp_rpt.typeButton == 1) {
        RF_RTCM_Chunk button_response;
        button_response.device_id = temp_rpt.device_id;
        button_response.type = TYPE_BUTTON_PRESSED;
        button_response.batch_id = 0;
        button_response.seq = 0;
        button_response.total = 1;

        send_single_to_rover(button_response);
        send_to_server();
      }

      // 👉 lưu rover
      uint8_t idx = temp_rpt.device_id - 1;
      memcpy(&ROVER_LIST[idx], &temp_rpt, sizeof(RF_Rover_Report));
      ROVER_MODE[idx] = temp_rpt.modeRTK;

      if (safety > 10) {
        SerialLog.println("⚠ FIFO overflow → flush");
        radio.flush_rx();
        break;
      }
    }

    last_rover_msg_ms = millis();
  }

  if (tx_fail) radio.flush_tx();
}

void rf_auto_recover() {
  static uint32_t last_recover = 0;

  if (millis() - last_rover_msg_ms < 5000) return;

  if (millis() - last_recover < 5000) return;

  last_recover = millis();

  static uint8_t level = 0;
  // test recovery rf
  if (level == 0) {
    REPEATER_LIST[2] = 1;
    SerialLog.println("RF flush recover");
    radio.stopListening();
    radio.flush_rx();
    radio.flush_tx();
    radio.startListening();
  }
  else if (level == 1) {
    REPEATER_LIST[3] = 1;
    SerialLog.println("RF power cycle");
    radio.powerDown();
    delay(5);
    radio.powerUp();
    delay(5);
    radio.startListening();
  }
  else {
    REPEATER_LIST[4] = 1;
    SerialLog.println("RF full reinit");
    RF_Init();
  }

  level++;
  if (level > 2) level = 2;
}

void get_and_aggregate_rtcm()
{
  if (uart_idle_flag) {
    uart_idle_flag = false;

    // Đọc toàn bộ cụm dữ liệu IDLE vừa báo
    uint16_t n = UART_Read(buff_data, sizeof(buff_data));

    if (n > 0) {
        // Đưa cả cụm vào hàng đợi lọc
      RTCM_Process(buff_data, n);
      cycle_done = false;
#ifdef DEBUG
      SerialLog.print("uart_n=");
      SerialLog.println(n);
#endif
    }
  }

  uint16_t pkg_len = 0;
  while (RTCM_GetPacket(buff_data, &pkg_len)) {
    memcpy(rtcm_data + rtcm_length, buff_data, pkg_len);
    rtcm_length += pkg_len;
  }
}

void process_rtcm_sending()
{
  uint32_t now = millis();
  if (!cycle_done && now - last_rf_tx_ms >= LAST_RF_TX_MS && now - last_rover_msg_ms > LAST_ROVER_MSG_MS) {
    uint16_t latest_start = find_latest_batch_start(rtcm_data, rtcm_length);
    send_rtcm_to_rover(batch_id++, latest_start);
#ifdef DEBUG
    SerialLog.print("Time_send=");
    SerialLog.print(millis() - now);
    SerialLog.print(", rtcm_length=");
    SerialLog.println(rtcm_length);
#endif

    last_rf_tx_ms = millis();
    cycle_done = true;
    rtcm_length = 0;
  }
}

uint16_t get_rtcm_msg_id(uint8_t* data, uint16_t pos) {
  return (uint16_t)((data[pos + 3] << 4) | (data[pos + 4] >> 4));
}

uint16_t find_latest_batch_start(uint8_t* data, uint16_t total_len) {
  if (total_len < 10) return 0;

  int16_t found_1074 = -1;
  int16_t i = total_len - 3;

  while (i >= 0) {
    if (data[i] == 0xD3) {
      uint16_t p_len = ((data[i + 1] & 0x03) << 8) | data[i + 2];
      uint16_t packet_size = p_len + 6;

      if (i + packet_size <= total_len) {
        uint16_t msg_id = (uint16_t)((data[i + 3] << 4) | (data[i + 4] >> 4));

        if (msg_id == 1005 || msg_id == 1006) {
          return i;
        }

        if (msg_id == RTCM_START_MSG_ID && found_1074 == -1) {
          found_1074 = i;
        }
      }
    }
    i--;
    if (found_1074 != -1 && (found_1074 - i) > 400) break;
  }
  return (found_1074 != -1) ? found_1074 : 0;
}

void send_rtcm_to_rover(uint8_t batch_id, uint16_t start_pos) {
  uint16_t actual_send_len = rtcm_length - start_pos;
  if (actual_send_len <= 0) return;

#ifdef DEBUG
  // --- LOG DANH SÁCH ID (Giữ lại 1124, 1124 nếu có trong cụm) ---
  String msg_log = "rtcms_send: ";
  uint16_t p = start_pos;
  while (p <= rtcm_length - 3) {
    if (rtcm_data[p] == 0xD3) {
      uint16_t len = ((rtcm_data[p + 1] & 0x03) << 8) | rtcm_data[p + 2];
      msg_log += String(get_rtcm_msg_id(rtcm_data, p)) + " ";
      p += (len + 6);
    } else { p++; }
  }
  SerialLog.println(msg_log);
#endif

  uint8_t total_chunks = (actual_send_len + RF_RTCM_CHUNK_DATA_SIZE - 1) / RF_RTCM_CHUNK_DATA_SIZE;
  if (total_chunks % 4 == 3) total_chunks++;

  radio.stopListening();
  RF_RTCM_Chunk pkt;
  for (uint8_t j = 0; j < total_chunks; j++) {
    pkt.device_id = DEVICE_ID;
    pkt.type = TYPE_RTCM;
    pkt.batch_id = batch_id;
    pkt.seq = j;
    pkt.total = total_chunks;

    memset(pkt.data, 0, RF_RTCM_CHUNK_DATA_SIZE);

    uint16_t current_offset = start_pos + (j * RF_RTCM_CHUNK_DATA_SIZE);
    if (current_offset < rtcm_length) {
      uint16_t bytes_left = rtcm_length - current_offset;
      uint16_t bytes_to_copy = (bytes_left > RF_RTCM_CHUNK_DATA_SIZE) ? RF_RTCM_CHUNK_DATA_SIZE : bytes_left;
      memcpy(pkt.data, &rtcm_data[current_offset], bytes_to_copy);
    }

    // Gửi No-Ack để tối ưu tốc độ và tránh nhiễu ACK từ nhiều Rover
    if (!radio.write(&pkt, sizeof(pkt))) {
        radio.flush_tx();
    }
    delayMicroseconds(DELAY_CHUNK_MICRO);
  }

  radio.startListening();
}

void send_single_to_rover(RF_RTCM_Chunk pkt)
{
  radio.stopListening();
  bool success = radio.write(&pkt, sizeof(pkt));
#ifdef ACK
  if (success) { SerialLog.println("report ok"); }
  else
  {
    radio.flush_tx();
    SerialLog.println("report fail");
  }
#endif
  delayMicroseconds(DELAY_CHUNK_MICRO);

  radio.startListening();
}

void send_report_to_server() {
  if (millis() - last_eth_report_send > 1000) {
    last_eth_report_send = millis();
    
    send_to_server();
  }
}

void int64_to_string(int64_t value, char *out) {
  char temp[32];
  int pos = 0;

  if (value < 0) {
    *out++ = '-';
    value = -value;
  }

  int64_t integer = value / 10000000000;
  int64_t fraction = value % 10000000000;

  int i = 0;
  do {
      temp[i++] = (integer % 10) + '0';
      integer /= 10;
  } while (integer > 0);

  // đảo ngược
  while (i--) {
      *out++ = temp[i];
  }

  *out++ = '.';

  // fraction đủ 10 số
  for (int i = 9; i >= 0; i--) {
    out[i] = (fraction % 10) + '0';
    fraction /= 10;
  }
  out += 10;

  *out = '\0';
}

uint16_t getMinTxFree() {
  uint16_t minFree = 0xFFFF;

  for (uint8_t i = 0; i < 8; i++) {
    uint16_t freeSize = W5100.readSnTX_FSR(i);

    if (freeSize < minFree) {
        minFree = freeSize;
    }
  }

  return minFree;
}

void send_to_server() {
  char buffer[2048];
  DynamicJsonDocument doc(2048); 
  JsonArray dataArray = doc.to<JsonArray>();

  for (int i = 0; i < MAX_ROVER; i++) {
    if(ROVER_MODE[i] == -1) // check online device
     continue;

    JsonObject obj = dataArray.createNestedObject();
    
    char time_buffer[10];
    uint32_t raw_time = ROVER_LIST[i].time;
    int hh = ((raw_time / 10000) + 7) % 24;
    int mm = (raw_time % 10000) / 100;
    int ss = (raw_time % 100);
    sprintf(time_buffer, "%02d:%02d:%02d", hh, mm, ss);

    obj["id"] = ROVER_LIST[i].device_id;
    obj["time"] = time_buffer;

    char lat_str[32];
    char lon_str[32];
    int64_to_string(ROVER_LIST[i].lat, lat_str);
    int64_to_string(ROVER_LIST[i].lon, lon_str);
    obj["lat"] = lat_str;
    obj["lon"] = lon_str;

    obj["battery"] = ROVER_LIST[i].battery;
    obj["modeRTK"] = ROVER_LIST[i].modeRTK;
    obj["typeButton"] = ROVER_LIST[i].typeButton;

    ROVER_MODE[i] = -1;
  }

  for (int i = 0; i < MAX_REPEATER; i++) {
    if (millis() - REPEATER_LAST_SEEN[i] > REPEATER_PING_TIMEOUT) {
      REPEATER_LIST[i] = 0;
    }
  }

  char repeater_buf[32] = {0};
  for (int i = 0; i < MAX_REPEATER; i++) {
    snprintf(repeater_buf + strlen(repeater_buf), sizeof(repeater_buf) - strlen(repeater_buf), 
      "%d%s", REPEATER_LIST[i], (i < MAX_REPEATER - 1) ? "-" : "");
  }
  
  JsonObject obj = dataArray.createNestedObject();
  obj["repeater_list"] = repeater_buf;

  size_t len = serializeJson(doc, buffer, sizeof(buffer));

#ifdef DEBUG_ 
  SerialLog.println(buffer);
#endif

  if (!lan_ready_flag || len <= 0) { 
    SerialLog.println("Lan_ready_flag failed or buffer length: 0");
    return;
  }

  if (Ethernet.linkStatus() != LinkON)
  { 
    SerialLog.println("UDP Link status failed - Skipping send");
    return;
  }

  uint16_t freeSize = getMinTxFree();

  if (freeSize > 512) {
    last_udp_healthy = millis();
  } 
  else {
    if (millis() - last_udp_healthy > 5000) {
        Udp.stop();
        delay(10);
        Udp.begin(LOCAL_PORT);
    }
  }

  if (freeSize < len) {
#ifdef DEBUG_
      SerialLog.println("TX buffer FULL → drop packet");
#endif
      return;
  }

  if (Udp.beginPacket(server_address, SERVER_PORT)) {
      Udp.write((uint8_t*)buffer, len);
      Udp.endPacket();
  }

//   if (lan_ready_flag  &&  len > 0 ) {// &&  server_status == STATUS_CONNECT) {
//     if (Udp.beginPacket(server_address, SERVER_PORT)) {
//       Udp.write((uint8_t*)buffer, len);
//       if (Udp.endPacket() == 0) { 
// #ifdef DEBUG_
//         SerialLog.println("UDP End Packet failed!"); 
// #endif
//       }
//       else { 
// #ifdef DEBUG
//         SerialLog.println("UDP Send success!");
// #endif
//       }
//     } 
//     else { 
// #ifdef DEBUG_
//       SerialLog.println("UDP Begin Packet failed - Skipping send"); 
// #endif
//     }
//   }
//   else { 
// #ifdef DEBUG_
//     SerialLog.println("Lan_ready_flag failed or buffer length is 0"); 
// #endif
//   }
  udp_ok = true;
}

void process_lan_config_mode() {
  loop_ok = rf_ok = udp_ok = true;
  check_watch_dog();

  if (cfg_force_exit) {

    is_in_config_mode = false;
    cfg_state = CFG_IDLE;
    cfg_queue.clear();
    cfg_response = "";
    cfg_force_exit = false;

    // flush UART
    uint8_t c;
    while (UART_Read(&c, 1) > 0);

    Udp.beginPacket(Udp.remoteIP(), Udp.remotePort());
    Udp.endPacket();
    
    loop_ok = rf_ok = udp_ok = false;

#ifdef DEBUG
    SerialLog.println("[CFG] Force exit");
#endif
    return;
  }

  if (millis() - last_udp_config > CONFIG_TIMEOUT * 2) {
    is_in_config_mode = false;
    cfg_state = CFG_IDLE;
    cfg_queue.clear();
    cfg_response = "";

    Udp.beginPacket(Udp.remoteIP(), Udp.remotePort());
    Udp.println("--- EXIT CONFIG MODE (TIMEOUT) ---");
    Udp.endPacket();
    loop_ok = rf_ok = udp_ok = false;
    return;
  }

  switch (cfg_state) {
    case CFG_IDLE:
      if (!cfg_queue.empty()) {
        cfg_cmd = cfg_queue.front();
        cfg_queue.pop_front();
        cfg_state = CFG_SEND_CMD;
      }
      break;
    case CFG_SEND_CMD:
      UART_Write_Queue((uint8_t*)cfg_cmd.c_str(), cfg_cmd.length());
      UART_Write_Queue((uint8_t*)"\r\n", 2);

#ifdef DEBUG
      SerialLog.print("[CFG SEND] "); SerialLog.println(cfg_cmd);
#endif

      cfg_start_time = millis();
      cfg_response = "";
      cfg_state = CFG_WAIT_RESP;
      break;
    case CFG_WAIT_RESP: {
      UART_DMA_Process_TX();

      uint8_t c;
      while (UART_Read(&c, 1) > 0) {
        if (c >= 32 && c <= 126) {
          cfg_response += (char)c;
        }
        else if (c == '\r' || c == '\n') {
          cfg_response += (char)c;
        }

        // avoid overflow
        if (cfg_response.length() > 512) {
          cfg_response.remove(0, 128);
        }
      }
      int start = cfg_response.indexOf("$command");


      String filtered = cfg_response;

      int idx = filtered.indexOf("$command");

      if (idx >= 0) {
        filtered = filtered.substring(idx);
      } else {
        // nếu không có $command → có thể giữ nguyên hoặc bỏ
        filtered = "[NO VALID COMMAND RESPONSE]\n" + filtered;
      }
        
      if (millis() - cfg_start_time > CONFIG_TIME_OUT_MS) {
        Udp.beginPacket(Udp.remoteIP(), Udp.remotePort());
        Udp.print("-> UM cmd: " + cfg_cmd);
        Udp.print(", response: ");
        Udp.println(filtered);
        Udp.endPacket();
#ifdef DEBUG
        SerialLog.println("[CFG RESP]"); SerialLog.println(cfg_response);
#endif
        cfg_state = CFG_IDLE;
      }
      break;
    }
  }
}

void handle_udp() {
  int packetSize = Udp.parsePacket();
  if (!packetSize) return;

  char remoteBuffer[256];
  int len = Udp.read(remoteBuffer, sizeof(remoteBuffer) - 1);
  if (len <= 0) return;
  remoteBuffer[len] = '\0';
#ifdef DEBUG
  SerialLog.print("UDP RX: ");
  SerialLog.println(remoteBuffer);
#endif

  String input = String(remoteBuffer);
  input.trim();

  IPAddress cfg_remote_ip = Udp.remoteIP();
  uint16_t cfg_remote_port = Udp.remotePort();

  if (input == CONFIG_START) {
    is_in_config_mode = true;
    cfg_force_exit = false;

    cfg_queue.clear();
    cfg_state = CFG_IDLE;

    Udp.beginPacket(cfg_remote_ip, cfg_remote_port);
    Udp.println("\n--- ENTERED CONFIG MODE ---");
    Udp.endPacket();

    last_udp_config = millis();
    return;
  }

  if (is_in_config_mode) {
    if (input == CONFIG_END) {
      cfg_force_exit = true;
      Udp.beginPacket(cfg_remote_ip, cfg_remote_port);
      Udp.println("--- EXIT CONFIG MODE (CMD) ---");
      Udp.endPacket();

      return;
    }
    // queue command
    if (cfg_queue.size() < CFG_QUEUE_MAX) {
      cfg_queue.push_back(input);
      Udp.beginPacket(cfg_remote_ip, cfg_remote_port);
      Udp.println("Queued: " + input);
      Udp.endPacket();
    } else {
      Udp.beginPacket(cfg_remote_ip, cfg_remote_port);
      Udp.println("Queue FULL!");
      Udp.endPacket();
    }

    last_udp_config = millis();
    return;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, remoteBuffer);

  if (error) {
#ifdef DEBUG
    SerialLog.println("JSON parse error");
#endif
    return;
  }

  if (doc.is<JsonArray>()) {
    JsonArray cmd_list = doc.as<JsonArray>();
    for (JsonObject item : cmd_list) {
      int type = item["type"];
      int dev_id = item["device_id"];
      switch (type) {
        case TYPE_NOTI: {
          RF_RTCM_Chunk rf_pkt;
          rf_pkt.device_id = dev_id;
          rf_pkt.type = TYPE_NOTI;
          rf_pkt.batch_id = 1;
          rf_pkt.seq = 0;
          rf_pkt.total = 1;
          rf_pkt.data[0] = 1;
          send_single_to_rover(rf_pkt);
          break;
        }
        case TYPE_PING:
          last_server_ping_ms = millis();
          break;
        default:
          break;
      }
      delayMicroseconds(DELAY_CHUNK_MICRO);
    }
  }
}

void TIMER_INIT() {
  StatusTimer = new HardwareTimer(STATUS_TIMER);
  StatusTimer->pause();
  StatusTimer->setOverflow(10, HERTZ_FORMAT);

  HAL_NVIC_SetPriority(TIM3_IRQn, 14, 0);

  StatusTimer->attachInterrupt(timer_callback);
  StatusTimer->resume();
}

void timer_callback() {
  system_ticks++;

  update_led();
}

void check_connection_status() {
  uint32_t now = millis();
  rover_status = now - last_rover_msg_ms < ROVER_TIMEOUT_MS ? STATUS_CONNECT : STATUS_DISCONNECT;
  server_status = now - last_server_ping_ms < SERVER_TIMEOUT_MS ? STATUS_CONNECT : STATUS_DISCONNECT;
}

void update_led() {
  rover_status == STATUS_DISCONNECT ? digitalWrite(LED_STATUS, HIGH) :  digitalWrite(LED_STATUS, LOW);
  server_status == STATUS_DISCONNECT ? digitalWrite(LED_LAN, HIGH) :  digitalWrite(LED_LAN, LOW);
}
