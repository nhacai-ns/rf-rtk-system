import socket
import json
import threading
import time
from datetime import datetime

# Định nghĩa các loại Packet (Khớp với phía Base/Rover)
TYPE_RTCM = 0
TYPE_REPORT = 1
TYPE_REPORT_REPEATED = 2
TYPE_BUTTON_PRESSED = 3
TYPE_NOTI = 4
TYPE_MSG = 5
TYPE_PING = 6

UDP_IP = "0.0.0.0"
UDP_PORT = 192

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((UDP_IP, UDP_PORT))

last_rover_addr = None

def receive_thread():
    global last_rover_addr
    print(f"--- RTK MULTI-ROVER UDP SERVER STARTED AT {UDP_PORT} ---")
    while True:
        try:
            data, addr = sock.recvfrom(8192) # Tăng buffer lên để nhận chuỗi danh sách dài
            last_rover_addr = addr 
            
            raw_str = data.decode('utf-8', errors='ignore').strip()
            payload = json.loads(raw_str)
            
            timestamp = datetime.now().strftime('%H:%M:%S')

            # KIỂM TRA NẾU PAYLOAD LÀ DANH SÁCH (Dữ liệu gộp từ Base)
            if isinstance(payload, list):
                print(f"\n[{timestamp}] Batch Report from {addr[0]} ({len(payload)} rovers):")
                for rpt in payload:
                    r_id = rpt.get("id")
                    r_lat = rpt.get("lat")
                    r_lon = rpt.get("lon")
                    r_bat = rpt.get("battery")
                    r_mode = rpt.get("modeRTK")
                    r_btn = rpt.get("typeButton")

                    # Logic hiển thị nhanh
                    status_str = f"ID:{r_id} | Lat:{r_lat} | Lon:{r_lon} | Bat:{r_bat}% | Mode:{r_mode} | Button:{r_btn}"
                    if r_btn != 0:
                        status_str += f" | [BUTTON EVENT:{r_btn}]"
                    print(f" > {status_str}")
            
            # XỬ LÝ NẾU LÀ GÓI ĐƠN (Ví dụ PING hoặc MSG đơn lẻ)
            elif isinstance(payload, dict):
                p_type = payload.get("type")
                if p_type == TYPE_PING:
                    continue
                
                device_id = payload.get("device_id", "Unknown")
                print(f"\n[{timestamp}] Single Packet from {addr[0]} (Dev {device_id}): {payload}")

            print("\nEnter 'q' to notify or message: ", end="", flush=True)
            
        except Exception as e:
            print(f"\n[Parse Error]: {e}")

def ping_loop():
    while True:
        if last_rover_addr:
            try:
                ping_cmd = {"device_id": 0, "type": TYPE_PING, "data": "SERVER_ALIVE"}
                sock.sendto(json.dumps(ping_cmd).encode('utf-8'), last_rover_addr)
            except: pass
        time.sleep(10)

# Khởi chạy các luồng
threading.Thread(target=receive_thread, daemon=True).start()
threading.Thread(target=ping_loop, daemon=True).start()

while True:
    try:
        user_input = input().strip()
        if not last_rover_addr:
            print("Wait for Base connection...")
            continue

        if user_input.lower() == 'q':
            command = {"device_id": 1, "type": TYPE_NOTI, "data": "OUT_OF_SAFE_ZONE"}
        else:
            command = {"device_id": 0, "type": TYPE_MSG, "data": user_input}

        sock.sendto(json.dumps(command).encode('utf-8'), last_rover_addr)
        print(f">>> Sent: {command}")
    except KeyboardInterrupt: break
    except Exception as e: print(f"Error: {e}")
