# WiFi Spectrum Display

A WiFi spectrum analyzer for ESP32-C3 that visualizes 2.4GHz WiFi activity on a NeoPixel LED strip. Monitor packet traffic or signal strength across all 13 WiFi channels in real-time.

![Device Photo](images/display%20working.png)

![Layout Diagram](images/wifi%20display-layout.svg)

## ⚠️ Migration Update: Native ESP-IDF

**This project has been ported to the native ESP-IDF framework.** The original Arduino implementation is preserved in the `wifi_spectrum_display/` directory for reference, but the primary development path is now the ESP-IDF project located in the root.

See [Migration Report](migration_report.md) for architectural details.

## Features

- **Two Scan Modes**
  - **Traffic Mode**: Passively monitors packet traffic per channel
  - **RSSI Mode**: Scans for access points and displays signal strength

- **Display Modes**
  - Solid color with adjustable hue/saturation
  - Colormaps (Jet, HSV, Autumn, Hot, Cool, Viridis, Plasma, Turbo)
  - Rainbow scroll
  - Cycling color

- **Web Configuration UI**
  - Connect to the device's WiFi AP to configure settings
  - Real-time preview of changes
  - Captive portal auto-opens settings page

- **Persistent Settings**
  - All settings saved to NVS (flash memory)
  - Survives reboots

- **Button Controls**
  - Short press: Cycle display mode
  - Double press: Toggle WiFi AP for settings
  - Long press (3s): Power on/off

- **RGB Button LED**
  - White breathing: Normal operation
  - Solid blue: AP mode active
  - Off: Powered down

## Hardware

### Components

| Component |QTY. | Description | Example Online Source |
|-----------|-------------|-------------|-------------|
| SeeedStudio Xiao ESP32-C3 | 1| Main microcontroller | https://www.seeedstudio.com/Seeed-XIAO-ESP32C3-p-5431.html|
| WS2812B Strip |1| 144led/m IP30 (i.e. no covering) ~30cm strip, usually sold per meter | https://www.aliexpress.com/item/1005009096063569.html|
| Momentary Button with RGB LED |1| Common cathode button LED 5v 16mm low profile momentary |https://www.aliexpress.com/item/4001160061412.html|
| M2.5X16mm  screws |5| Countersunk head |https://www.aliexpress.com/item/32946954901.html|
| M2.5X8mm  screws |2| Countersunk head |https://www.aliexpress.com/item/32946954901.html|

### Wiring

| Function | GPIO Pin |
|----------|----------|
| NeoPixel Data | 4        |
| Button | 9        |
| Button LED Red | 1        |
| Button LED Green | 3        |
| Button LED Blue | 2        |

Connect NeoPixel VCC to 5V and GND to GND.

### 3D Printed Enclosure

Print files are located in the `3D prints` folder. The model is already oriented for printing and no supports are necessary. I think 0.15-0.2mm layer height is good.

## Software Setup (ESP-IDF)

### Prerequisites

- [ESP-IDF v5.0+](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/get-started/)
- `esptool.py` (usually included with ESP-IDF)

### Build & Flash

1.  **Set up environment**:
    ```bash
    . $HOME/esp/esp-idf/export.sh
    ```

2.  **Configuration** (Optional):
    ```bash
    idf.py menuconfig
    ```

3.  **Build**:
    ```bash
    idf.py build
    ```

4.  **Flash**:
    ```bash
    idf.py -p PORT flash monitor
    ```
    Replace `PORT` with your device's serial port (e.g., `COM3`, `/dev/ttyUSB0`).

### Project Structure

```
├── main/
│   ├── wifi_spectrum_display.c  # Main app entry point
│   ├── led_control.c            # LED strip driver & effects
│   ├── wifi_scanner.c           # WiFi scanner logic
│   ├── web_ui.c                 # HTTP server
│   ├── dns_server.c             # Captive portal DNS
│   ├── button.c                 # Button input & LED control
│   ├── storage.c                # NVS Settings
│   └── index.html               # Embedded Web UI
├── wifi_spectrum_display/       # Legacy Arduino Sketch
└── CMakeLists.txt               # Build configuration
```

## Legacy Arduino Setup

If you prefer using the Arduino IDE, the original source code is available in the `wifi_spectrum_display` folder.

1. Install [Arduino IDE](https://www.arduino.cc/en/software)
2. Add ESP32 board support
3. Install **FastLED** library
4. Open `wifi_spectrum_display/wifi_spectrum_display.ino`
5. Upload to your board

## Usage

### First Boot

1. Power on the device
2. The LED strip will show a startup animation
3. The button LED will breathe white

### Accessing Settings

1. Double-press the button (LED turns blue)
2. Connect to WiFi network: `WiFi-Spectrum`
3. Password: `spectrum123`
4. Settings page should open automatically (captive portal)
5. Or visit http://192.168.4.1
6. Configure display mode, colors, brightness, etc.
7. Click "Save & Exit" or double-press button to save and close AP

### Settings

| Setting | Description |
|---------|-------------|
| Display Mode | Solid Color, Colormap, Rainbow Scroll, Cycling Color |
| Scan Mode | RSSI (signal strength) or Traffic (packet count) |
| Colormap | Choose from 8 scientific colormaps |
| Solid Color | Hue and saturation for solid color mode |
| Brightness | NeoPixel strip brightness (0-100%) |
| Button LED | Button LED brightness (0-100%) |
| Max Current | Power limiting for LED strip (100-5000mA) |

## How It Works

### Traffic Mode (Default)

The ESP32 enters WiFi promiscuous mode and passively listens for all WiFi packets on each channel. It hops through all 13 channels, counting packets on each. Channels with more traffic appear brighter on the LED strip.

### RSSI Mode

The ESP32 scans for nearby access points and maps their signal strength to the LED strip based on which channel they're on. Stronger signals appear brighter.

### LED Mapping

The LEDs are mapped across 13 WiFi channels using a Gaussian kernel for smooth blending between adjacent channels. Each channel's activity spreads across multiple LEDs with a bell-curve distribution.

## License

MIT License - Feel free to use and modify.