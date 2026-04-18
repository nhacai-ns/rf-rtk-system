import socket
import json
import threading
import time
from datetime import datetime

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

# NEW: tracking rover state
rover_last_seen = {}   # {id: timestamp}
rover_state = {}       # {id: "ACTIVE / WEAK / LOST"}

LOCK = threading.Lock()

def update_rover(id):
    now = time.time()
    with LOCK:
        if id not in rover_last_seen:
            print(f"🟢 Rover {id} FIRST SEEN → ACTIVE")

        rover_last_seen[id] = now
        rover_state[id] = "ACTIVE"

def check_timeout_loop():
    while True:
        now = time.time()
        with LOCK:
            for rid in list(rover_last_seen.keys()):
                dt = now - rover_last_seen[rid]

                if dt > 20:
                    if rover_state.get(rid) != "DISCONNECTED":
                        rover_state[rid] = "DISCONNECTED"
                        print(f"🔴 Rover {rid} DISCONNECTED (>30s)")
                
                elif dt > 5:
                    if rover_state.get(rid) != "RF_WEAK":
                        rover_state[rid] = "RF_WEAK"
                        print(f"🟠 Rover {rid} RF WEAK (>5s)")
                
                else:
                    rover_state[rid] = "ACTIVE"

        time.sleep(1)

def receive_thread():
    global last_rover_addr
    print(f"--- RTK MULTI-ROVER UDP SERVER STARTED AT {UDP_PORT} ---")

    while True:
        try:
            data, addr = sock.recvfrom(8192)
            last_rover_addr = addr 
            
            raw_str = data.decode('utf-8', errors='ignore').strip()
            timestamp = datetime.now().strftime('%H:%M:%S')

            # 🔥 IN RAW PAYLOAD
            print(f"\n[{timestamp}] RAW from {addr[0]}:")
            print(raw_str)

            payload = json.loads(raw_str)

            # =====================================================
            # 📦 BATCH JSON (LIST)
            # =====================================================
            if isinstance(payload, list):
                print(f"[{timestamp}] Batch ({len(payload)} items):")

                for item in payload:

                    # 🔥 CHỈ LỌC JSON CÓ FIELD "id"
                    if not isinstance(item, dict) or "id" not in item:
                        continue

                    r_id = item.get("id")
                    r_lat = item.get("lat")
                    r_lon = item.get("lon")
                    r_bat = item.get("battery")
                    r_mode = item.get("modeRTK")
                    r_btn = item.get("typeButton", 0)

                    # update rover state
                    update_rover(r_id)

                    state = rover_state.get(r_id, "UNKNOWN")

                    status_str = (
                        f"ID:{r_id} | Lat:{r_lat} | Lon:{r_lon} | "
                        f"Bat:{r_bat}% | Mode:{r_mode} | State:{state}"
                    )

                    if r_btn != 0:
                        status_str += f" | [BUTTON:{r_btn}]"

                    print(f" > {status_str}")

            # =====================================================
            # 📦 SINGLE JSON (DICT)
            # =====================================================
            elif isinstance(payload, dict):

                # 🔥 CHỈ xử lý nếu có "id"
                if "id" not in payload:
                    print(f"[{timestamp}] Skip non-rover JSON")
                    return

                r_id = payload.get("id")
                update_rover(r_id)

                print(f"[{timestamp}] Single Rover:")
                print(payload)

            print("\nEnter number 1 - 5 to notify or message: ", end="", flush=True)

        except Exception as e:
            print(f"\n[Parse Error]: {e}")
            
def ping_loop():
    while True:
        if last_rover_addr:
            try:
                ping_cmd = [{"device_id": 0, "type": TYPE_PING, "data": "SERVER_ALIVE"}]
                sock.sendto(json.dumps(ping_cmd).encode('utf-8'), last_rover_addr)
            except: pass
        time.sleep(10)

# NEW: start timeout checker
threading.Thread(target=check_timeout_loop, daemon=True).start()
threading.Thread(target=receive_thread, daemon=True).start()
threading.Thread(target=ping_loop, daemon=True).start()

while True:
    try:
        user_input = input("\nInput ID list (Ex: 1 3 5) or 'q' to exit: ").strip()
        
        if not last_rover_addr:
            print("No connection from Base...")
            continue
        
        if user_input.lower() == 'q':
            break

        try:
            target_ids = [int(id_str) for id_str in user_input.split()]
            
            if not target_ids:
                print("Input at least 1 device!")
                continue

            batch_command = []
            for dev_id in target_ids:
                cmd_item = {
                    "device_id": dev_id,
                    "type": TYPE_NOTI,
                    "data": "1" 
                }
                batch_command.append(cmd_item)

            json_payload = json.dumps(batch_command)
            sock.sendto(json_payload.encode('utf-8'), last_rover_addr)
            
            print(f">>> Payload: {json_payload}")

        except ValueError:
            single_command = [{
                "device_id": 0, 
                "type": TYPE_MSG, 
                "data": user_input
            }]
            sock.sendto(json.dumps(single_command).encode('utf-8'), last_rover_addr)
            print(f">>> Sent to Base: {user_input}")

    except KeyboardInterrupt: 
        break
    except Exception as e: 
        print(f"Data error: {e}")