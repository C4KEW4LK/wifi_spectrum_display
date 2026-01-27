# Migration Report: Arduino to ESP-IDF

## Overview
The WiFi Spectrum Display project has been ported from a monolithic Arduino sketch (`.ino`) to a modular native ESP-IDF C project. This improves performance, control, and maintainability.

## Architecture
The project is split into the following components in `main/`:

1.  **`wifi_spectrum_display.c`**: Main entry point (`app_main`). Orchestrates the main loop, state management (Power/AP), and event handling.
2.  **`led_control.c`**: Manages the WS2812 LED strip using the RMT peripheral. Implements effects (Gaussian spread, decay, color modes) and `led_strip` driver integration.
3.  **`wifi_scanner.c`**: Handles WiFi initialization, promiscuous mode packet capturing, channel hopping, and **RSSI active scanning**.
4.  **`web_ui.c`**: Implements the HTTP server and API endpoints. Serves the embedded `index.html`.
5.  **`dns_server.c`**: Implements a Captive Portal (UDP Port 53) that redirects all queries to the SoftAP IP.
6.  **`button.c`**: Handles GPIO input with software debouncing and RGB LED control using `ledc` (PWM).
7.  **`storage.c`**: Manages NVS (Non-Volatile Storage) for persisting settings, replacing Arduino `Preferences`.

## Key Changes & Replacements

| Feature | Arduino Library | ESP-IDF Implementation |
| :--- | :--- | :--- |
| **Framework** | Arduino Core | ESP-IDF Native (FreeRTOS) |
| **LEDs** | `FastLED` | `espressif/led_strip` (RMT) + Custom color logic |
| **WiFi** | `WiFi.h` | `esp_wifi`, `esp_event`, `esp_netif` |
| **Web Server** | `WebServer` | `esp_http_server` |
| **DNS** | `DNSServer` | Native UDP Socket (`dns_server.c`) |
| **Settings** | `Preferences` | `nvs_flash` / `nvs` |
| **OTA** | `ArduinoOTA` | *Removed* (Use `esp_https_ota` in future) |
| **Promiscuous** | Wrapped `esp_wifi` | Native `esp_wifi` promiscuous mode |

## Build System
- Uses `CMake` and `idf_component_manager`.
- `idf_component.yml` handles the `led_strip` dependency.
- `index.html` is embedded directly into the binary using `EMBED_TXTFILES`.
