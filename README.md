# 📡 RF-RTK Positioning System (STM32F411 + RF24 + W5500)
A high-precision RTK positioning system based on STM32F411CEU6 microcontroller, achieving 1–2 cm accuracy, and utilizing RF24 wireless communication and Ethernet connectivity for reliable real-time data transmission.
---
## 🚀 Overview
This project implements a Real-Time Kinematic (RTK) positioning system using an STM32F4 microcontroller. The system combines:
* High-precision GNSS (RTK)
* RF24 wireless communication (2.4GHz)
* Ethernet networking via W5500
It is designed for applications requiring **centimeter-level accuracy**, such as:
* Robotics navigation
* Autonomous vehicles
* Surveying & mapping
* Industrial positioning systems
---
## 🧩 Hardware Configuration
### 🔹 Microcontroller
* **MCU:** STM32F411CEU6
* Core: ARM Cortex-M4
* Clock: up to 100 MHz
---
### 🔹 RF Communication Module
* **Module:** nRF24L01 / nRF24L01+
* Frequency: 2.4 GHz
* Interface: SPI
---
### 🔹 Ethernet Module
* **Module:** W5500 Ethernet Controller
* Interface: SPI
* Protocol: TCP/IP (hardware offload)
---
### 🔹 GNSS Module (RTK)
* Recommended: UM980 (or equivalent RTK-capable module)
---
## 📚 Required Libraries
> 📌 You can update this section later with exact versions
| Library Name | Version | Notes |
|-------------|--------|------|
| ArduinoJson | >= 6.x | JSON parsing and serialization |
| RF24 | >= 1.4.x | nRF24L01 wireless communication |

---
## ⚙️ Development Environment
* Arduino IDE (STM32Duino core)
* STM32 Board Package (via Board Manager)
---
## 🔧 Installation Guide
### 1. Install STM32 Board Support
* Open Arduino IDE
* Go to **Boards Manager**
* Search: `STM32`
* Install: **STM32duino**
---
### 2. Install Required Libraries
* Open **Library Manager**
* Install libraries listed in section above
---
### 3. Clone Repository
```bash
git clone https://github.com/your-username/rf-rtk-system.git
```
---
### 4. Build & Upload
* Open project in Arduino IDE
* Select board: **STM32F411CEU6**
* Select correct COM port
* Upload firmware
---
## 🧠 System Architecture
```
GNSS (RTK) ---> STM32F411 ---> RF24 ---> Remote Node
                      |
                      ---> W5500 Ethernet ---> PC / Server
```
---
## 📡 Key Features

* 🎯 RTK positioning accuracy: **1–2 cm**
* 📶 Wireless communication via nRF24L01
* 🌐 Ethernet connectivity using W5500
* ⚡ Real-time embedded processing
* 🔄 Low-latency data transmission
---
## ⚠️ Hardware Notes (IMPORTANT)
### 🔋 nRF24L01 Stability
* Use external capacitor: **10µF – 100µF**
* Ensure stable 3.3V power supply
* Avoid using weak onboard regulator
---
### 🔌 SPI Bus Considerations
* RF24 and W5500 share SPI → cần:
  * Separate **CS pins**
  * Proper SPI speed configuration
---
### 🌐 Ethernet (W5500)
* Ensure correct SPI mode
* Check PHY link status before transmission
---
## 🔄 Future Improvements
* OTA firmware update
* Watchdog & fail-safe system
* RF channel auto-switching
* Data encryption
* RTK base-rover optimization
---
## 👨‍💻 Author
Embedded RTK system developed on STM32 platform with RF and Ethernet communication.
