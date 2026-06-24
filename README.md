# Wireless Geotagging Camera System

A distributed embedded camera system that captures, streams, and geotags high-resolution images. The architecture consists of an ESP32-CAM acting as a wireless image server, and a main ESP32 Display Controller that handles real-time video decoding, GPS data parsing, and SD card file management.

## System Architecture

The project is split across two microcontrollers to optimize memory and processing power:

1. **Camera Node (`Esp32Cam_TFT.ino`):** * Configured as a WiFi Access Point (`CAM_AP`).
   * Runs a lightweight HTTP server serving QVGA JPEG frames directly from the OV2640 PSRAM buffer.
2. **Display & Control Node (`TFT_toggle_switch.ino`):**
   * Connects to the Camera Node via WiFi.
   * Utilizes `LovyanGFX` for high-speed 8-bit parallel TFT rendering.
   * Integrates a GPS module to parse NMEA sentences via `TinyGPS++`.
   * Manages state-machine logic for a "Live Viewfinder" vs. "Frozen" state, allowing the user to save geotagged images to an SD card.

## Key Features
* **Wireless Video Pipeline:** Real-time frame fetching using HTTP GET requests buffered into a dynamic 60KB RAM allocation.
* **Hardware User Interface:** Physical tactile buttons for `CAPTURE/RESUME` and `SAVE` functionalities with integrated debounce logic.
* **Real-Time Geotagging:** GPS coordinates (Latitude/Longitude), altitude, speed, and UTC timestamps are parsed in real-time and displayed on the UI.
* **Dynamic File Management:** Images saved to the SD card are automatically named using sanitized strings containing the exact timestamp and GPS coordinates (e.g., `IMG_2026-6-24_11-02-49_LatXX.X_LonXX.X.jpg`).

## Hardware Pin Mapping (Display Node)

| Component | Function | ESP32 Pin |
| :--- | :--- | :--- |
| **TFT Display** | 8-Bit Parallel Data | GPIO 1-8 |
| **TFT Control** | WR, RS, CS, RST | 35, 37, 38, 39 |
| **SD Card** | SPI (CS, MOSI, CLK, MISO) | 10, 11, 12, 13 |
| **GPS Module**| UART RX | 16 |
| **Buttons** | Capture, Save (Pullup) | 14, 15 |

## Data Structures & Memory Management
* **Frame Buffering:** Implements a dynamically allocated 60KB `imgBuf` to safely transfer chunked HTTP stream data before pushing the complete JPEG payload to the display driver.
* **String Sanitization Algorithm:** Custom string manipulation logic replaces invalid filesystem characters (`/`, `:`, ` `) with underscores and dashes to ensure FAT32 SD card compatibility.
* **Graceful Degradation:** The system gracefully handles GPS signal loss by utilizing an alternative naming convention (`_NoGPS.jpg`) and short-circuiting to standard `millis()` timestamps if filename limits are exceeded.

## Getting Started
1. **Flash Node 1:** Upload `Esp32Cam_TFT.ino` to your ESP32-CAM module.
2. **Flash Node 2:** Upload `TFT_toggle_switch.ino` to your main display board.
3. **Execution:** Power both boards. The Display Node will automatically connect to `CAM_AP` and initialize the live viewfinder. Ensure your GPS module has a clear view of the sky to achieve a satellite fix (indicated by the UI turning green).
