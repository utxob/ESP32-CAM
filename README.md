# ESP32-CAM WiFi Video Streaming & Recording

This project allows you to turn your **ESP32-CAM** into a live **WiFi video streaming** device with the ability to **capture photos**, **record MJPEG video to SD card**, and **control the flash** using physical buttons.

## Features

- Connects to WiFi and streams live video via browser
- Capture JPEG photos through the browser
- Record MJPEG video to SD card using a button
- Control Flash LED on/off with another button
- Web UI with buttons for start/stop stream and capture

---

## Hardware Required

- **ESP32-CAM** (AI-Thinker module)
- **FTDI USB to Serial Adapter** (for flashing)
- **MicroSD Card** (FAT32 formatted)
- 2x **Push Buttons**
- Jumper wires, breadboard (optional)

---

## Pin Connections

| ESP32-CAM Pin | Purpose               |
|---------------|------------------------|
| GPIO 12       | Video Record Toggle Button |
| GPIO 13       | Flash LED Toggle Button   |
| GND           | Ground for both buttons   |
| 5V & GND      | Power from FTDI           |
| U0R, U0T      | Connect to FTDI TX, RX    |
| IO0 -> GND    | **Must be LOW to flash**  |

---

## Arduino IDE Setup

1. Install **ESP32 Board Support** via Board Manager:
   - URL: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`

2. Go to **Tools > Board** and select:  
   `ESP32 Wrover Module`

3. Set these options:
   - Flash Mode: **QIO**
   - Flash Size: **4MB (with SPIFFS)**
   - Partition Scheme: **Default**
   - Upload Speed: **115200**

---

## Code Configuration

Edit the following lines in the sketch:

```cpp
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
```
**Accessing the Web Interface**


1. Open Serial Monitor (115200 baud)


2. Wait for connection to WiFi


3. It will show something like:

Stream Ready! Go to: http://192.168.0.104


4. Open browser and visit that address


5. You will see:

**Start Stream**

**Capture Photo**

**Flash Toggle**

**Record Video Toggle**




Videos and photos are saved to the SD card.


---
