/*
 * WiFi Spectrum Display for ESP32-C3
 *
 * Scans 2.4GHz WiFi channels and displays signal strength on NeoPixels.
 * Includes a web UI for selecting display modes and colors.
 *
 * Hardware:
 *   - ESP32-C3 board
 *   - WS2812B NeoPixel strip (50 LEDs)
 *   - Connect NeoPixel DATA to GPIO 8
 *   - Connect NeoPixel VCC to 5V, GND to GND
 *
 * Web UI:
 *   - Connect to WiFi AP "WiFi-Spectrum" (password: "spectrum123")
 *   - Open http://192.168.4.1 in browser
 *
 * Libraries required:
 *   - FastLED
 *   - WebServer (built-in with ESP32)
 */

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ArduinoOTA.h>
#include <FastLED.h>
#include <Preferences.h>
#include "esp_wifi.h"

// Sine lookup table for breathing effect (64 entries, 0-255 range)
const uint8_t BREATHING_LUT[64] = {
  128, 140, 152, 165, 176, 188, 198, 208,
  218, 226, 234, 240, 245, 250, 253, 255,
  255, 255, 253, 250, 245, 240, 234, 226,
  218, 208, 198, 188, 176, 165, 152, 140,
  128, 115, 103,  90,  79,  67,  57,  47,
   37,  29,  21,  15,  10,   5,   2,   0,
    0,   0,   2,   5,  10,  15,  21,  29,
   37,  47,  57,  67,  79,  90, 103, 115
};

Preferences preferences;

// =============================================================================
// CONFIGURATION
// =============================================================================

#define NEOPIXEL_PIN      8
#define BUTTON_PIN        5       // Button to toggle AP/web UI mode (pull-up, active LOW)
#define BUTTON_LED_R      4       // RGB button LED - Red
#define BUTTON_LED_G      3       // RGB button LED - Green
#define BUTTON_LED_B      2       // RGB button LED - Blue
#define NUM_CHANNELS      13
#define NUM_LEDS          46

#define LONG_PRESS_MS     3000    // Hold time for long press (power off)
#define DOUBLE_PRESS_MS   400     // Max time between presses for double press
#define DEBOUNCE_MS       50      // Button debounce time

#define SCAN_DELAY_MS     33
#define BRIGHTNESS        255

#define RSSI_MIN          -90
#define RSSI_MAX          -30

// Traffic monitoring settings
#define CHANNEL_HOP_MS    10       // Time to listen on each channel (~25 updates/s)
#define PACKETS_PER_MS    2       // Expected packets/ms on a busy channel
#define PACKETS_MIN       0       // Min packets for display scaling
#define PACKETS_MAX       (CHANNEL_HOP_MS * PACKETS_PER_MS)  // Auto-scaled to hop time

// Gaussian kernel for LED spread
#define KERNEL_RADIUS     6        // Kernel extends ±6 pixels from center
#define KERNEL_SIZE       (2 * KERNEL_RADIUS + 1)

// Decay settings
#define DECAY_INTERVAL_MS 20       // How often decay runs (independent of packet updates)
#define DECAY_FACTOR      0.95f     // Multiply brightness by this each tick (exponential decay)

// WiFi AP settings
const char* AP_SSID = "WiFi-Spectrum";
const char* AP_PASS = "spectrum123";

// OTA settings
const char* OTA_HOSTNAME = "wifi-spectrum";
const char* OTA_PASSWORD = "spectrum123";

// =============================================================================
// Display modes
// =============================================================================

enum DisplayMode {
  MODE_SOLID_COLOR,      // User-selected color, brightness = signal
  MODE_COLORMAP,         // Colormap based on RSSI
  MODE_RAINBOW_SCROLL,   // Scrolling rainbow, brightness = signal
  MODE_CYCLING_COLOR     // Single hue that cycles, brightness = signal
};

enum Colormap {
  CMAP_JET,              // Blue -> Cyan -> Green -> Yellow -> Red
  CMAP_HSV,              // Full rainbow
  CMAP_AUTUMN,           // Red -> Orange -> Yellow
  CMAP_HOT,              // Black -> Red -> Yellow -> White
  CMAP_COOL,             // Cyan -> Magenta
  CMAP_VIRIDIS,          // Purple -> Blue -> Green -> Yellow
  CMAP_PLASMA,           // Purple -> Pink -> Orange -> Yellow
  CMAP_TURBO,            // Improved rainbow (Google's turbo)
  CMAP_COUNT             // Number of colormaps
};

enum ScanMode {
  SCAN_RSSI,             // Scan for APs and show signal strength
  SCAN_TRAFFIC           // Monitor packet traffic per channel
};

// =============================================================================
// Global variables
// =============================================================================

CRGB leds[NUM_LEDS];
int channelRSSI[NUM_CHANNELS];
volatile uint32_t channelPackets[NUM_CHANNELS];    // Packet counts per channel
uint32_t channelPacketsDisplay[NUM_CHANNELS];      // Display values for each channel
uint8_t ledDisplayBrightness[NUM_LEDS];            // Current brightness per LED (with decay)
float gaussianKernel[KERNEL_SIZE];                 // Pre-computed Gaussian kernel

// Dynamic packet scaling
uint32_t maxPacketsLastSecond = 1;                 // Max packets seen in last second (for scaling)
uint32_t currentSecondMaxPackets = 0;              // Max packets being tracked this second
unsigned long lastMaxReset = 0;                    // When we last reset the max tracker

// Dynamic RSSI scaling (over 10 seconds)
int rssiMin10s = -90;                              // Min RSSI seen in last 10s
int rssiMax10s = -30;                              // Max RSSI seen in last 10s
int currentRssiMin = 0;                            // Min being tracked this period
int currentRssiMax = -100;                         // Max being tracked this period
unsigned long lastRssiReset = 0;                   // When we last reset RSSI tracking

WebServer server(80);
DNSServer dnsServer;
const byte DNS_PORT = 53;

// Scan mode
ScanMode currentScanMode = SCAN_TRAFFIC;
uint8_t currentChannel = 1;
unsigned long lastChannelHop = 0;
bool promiscuousEnabled = false;

// Display settings
DisplayMode currentMode = MODE_SOLID_COLOR;
Colormap currentColormap = CMAP_JET;
uint8_t solidHue = 64;        // HSV hue for solid color (0-255) - pure yellow
uint8_t solidSat = 255;       // HSV saturation
uint8_t rainbowOffset = 0;    // For scrolling rainbow
uint8_t cyclingHue = 0;       // For cycling color mode
uint8_t maxBrightness = 255;  // Global brightness limit (0-255)
uint8_t buttonLedBrightness = 255;  // Button LED brightness (0-255)
uint16_t maxMilliamps = 2000; // Max current in mA (for power limiting)

// Timing
unsigned long lastScan = 0;
unsigned long lastAnimation = 0;
unsigned long lastDecay = 0;

// Button state
unsigned long buttonPressStart = 0;
unsigned long lastDebounceTime = 0;
bool lastButtonState = HIGH;
bool debouncedState = HIGH;
bool buttonHandled = false;
bool apEnabled = false;
bool powerOn = true;                // Power state (long press toggles)

// =============================================================================
// Web UI HTML
// =============================================================================

const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>WiFi Spectrum Display</title>
  <style>
    * { box-sizing: border-box; }
    body {
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
      background: linear-gradient(135deg, #1a1a2e 0%, #16213e 100%);
      color: #fff;
      margin: 0;
      padding: 20px;
      min-height: 100vh;
    }
    .container {
      max-width: 400px;
      margin: 0 auto;
    }
    h1 {
      text-align: center;
      font-size: 1.5em;
      margin-bottom: 30px;
      text-shadow: 0 0 20px rgba(0,150,255,0.5);
    }
    .card {
      background: rgba(255,255,255,0.1);
      border-radius: 15px;
      padding: 20px;
      margin-bottom: 20px;
      backdrop-filter: blur(10px);
    }
    .card h2 {
      margin-top: 0;
      font-size: 1.1em;
      opacity: 0.9;
    }
    .mode-btn {
      display: block;
      width: 100%;
      padding: 15px;
      margin: 10px 0;
      border: 2px solid rgba(255,255,255,0.2);
      border-radius: 10px;
      background: rgba(255,255,255,0.05);
      color: #fff;
      font-size: 1em;
      cursor: pointer;
      transition: all 0.3s;
    }
    .mode-btn:hover {
      background: rgba(255,255,255,0.15);
      border-color: rgba(255,255,255,0.4);
    }
    .mode-btn.active {
      background: rgba(0,150,255,0.3);
      border-color: #0af;
      box-shadow: 0 0 20px rgba(0,150,255,0.3);
    }
    .color-picker-wrap {
      display: flex;
      align-items: center;
      gap: 15px;
      margin-top: 15px;
    }
    .color-picker-wrap label {
      opacity: 0.8;
    }
    input[type="color"] {
      width: 60px;
      height: 40px;
      border: none;
      border-radius: 8px;
      cursor: pointer;
      background: none;
    }
    input[type="range"] {
      flex: 1;
      height: 8px;
      border-radius: 4px;
      -webkit-appearance: none;
      background: rgba(255,255,255,0.2);
    }
    input[type="range"]::-webkit-slider-thumb {
      -webkit-appearance: none;
      width: 20px;
      height: 20px;
      border-radius: 50%;
      background: #fff;
      cursor: pointer;
    }
    .status {
      text-align: center;
      opacity: 0.6;
      font-size: 0.9em;
      margin-top: 20px;
    }
    #colorSection, #colormapSection {
      display: none;
    }
    .preview {
      height: 30px;
      border-radius: 8px;
      margin-top: 10px;
      background: linear-gradient(90deg, #f00, #ff0, #0f0, #0ff, #00f, #f0f, #f00);
    }
    #solidPreview {
      background: hsl(0, 100%, 50%);
    }
    .cmap-btn {
      display: inline-block;
      padding: 10px 12px;
      margin: 5px;
      border: 2px solid rgba(255,255,255,0.2);
      border-radius: 8px;
      background: rgba(255,255,255,0.05);
      color: #fff;
      font-size: 0.9em;
      cursor: pointer;
      transition: all 0.3s;
    }
    .cmap-btn:hover {
      background: rgba(255,255,255,0.15);
    }
    .cmap-btn.active {
      background: rgba(0,150,255,0.3);
      border-color: #0af;
    }
    .cmap-grid {
      display: flex;
      flex-wrap: wrap;
      justify-content: center;
    }
    .cmap-preview {
      height: 6px;
      border-radius: 3px;
      margin-top: 5px;
    }
    .cmap-jet { background: linear-gradient(90deg, #000080, #00f, #0ff, #ff0, #f00); }
    .cmap-hsv { background: linear-gradient(90deg, #f00, #ff0, #0f0, #0ff, #00f, #f0f, #f00); }
    .cmap-autumn { background: linear-gradient(90deg, #f00, #f80, #ff0); }
    .cmap-hot { background: linear-gradient(90deg, #000, #f00, #ff0, #fff); }
    .cmap-cool { background: linear-gradient(90deg, #0ff, #f0f); }
    .cmap-viridis { background: linear-gradient(90deg, #440154, #3b528b, #21918c, #5ec962, #fde725); }
    .cmap-plasma { background: linear-gradient(90deg, #0d0887, #7e03a8, #cc4778, #f89540, #f0f921); }
    .cmap-turbo { background: linear-gradient(90deg, #30123b, #4686e1, #2ddcba, #f9dc2a, #e5500e, #7a0403); }
  </style>
</head>
<body>
  <div class="container">
    <h1>WiFi Spectrum Display</h1>

    <div class="card">
      <h2>Scan Mode</h2>
      <div class="cmap-grid">
        <button class="mode-btn scan-btn" data-scan="0" onclick="setScan(0)" style="display:inline-block;width:45%">
          RSSI
        </button>
        <button class="mode-btn scan-btn" data-scan="1" onclick="setScan(1)" style="display:inline-block;width:45%">
          Traffic
        </button>
      </div>
      <div class="status" style="margin-top:10px;font-size:0.85em" id="scanInfo">
        Scanning access points
      </div>
    </div>

    <div class="card">
      <h2>Display Mode</h2>
      <button class="mode-btn" data-mode="1" onclick="setMode(1)">
        Colormap (Heat Map)
      </button>
      <button class="mode-btn" data-mode="0" onclick="setMode(0)">
        Solid Color
      </button>
      <button class="mode-btn" data-mode="2" onclick="setMode(2)">
        Rainbow Scroll
      </button>
      <button class="mode-btn" data-mode="3" onclick="setMode(3)">
        Cycling Color
      </button>
    </div>

    <div class="card" id="colorSection">
      <h2>Color Selection</h2>
      <div class="color-picker-wrap">
        <label>Hue:</label>
        <input type="range" id="hueSlider" min="0" max="255" value="0" oninput="setColor()">
      </div>
      <div class="color-picker-wrap">
        <label>Saturation:</label>
        <input type="range" id="satSlider" min="0" max="255" value="255" oninput="setColor()">
      </div>
      <div id="solidPreview" class="preview"></div>
    </div>

    <div class="card" id="colormapSection">
      <h2>Colormap</h2>
      <div class="cmap-grid">
        <button class="cmap-btn" data-cmap="0" onclick="setCmap(0)">
          Jet<div class="cmap-preview cmap-jet"></div>
        </button>
        <button class="cmap-btn" data-cmap="1" onclick="setCmap(1)">
          HSV<div class="cmap-preview cmap-hsv"></div>
        </button>
        <button class="cmap-btn" data-cmap="2" onclick="setCmap(2)">
          Autumn<div class="cmap-preview cmap-autumn"></div>
        </button>
        <button class="cmap-btn" data-cmap="3" onclick="setCmap(3)">
          Hot<div class="cmap-preview cmap-hot"></div>
        </button>
        <button class="cmap-btn" data-cmap="4" onclick="setCmap(4)">
          Cool<div class="cmap-preview cmap-cool"></div>
        </button>
        <button class="cmap-btn" data-cmap="5" onclick="setCmap(5)">
          Viridis<div class="cmap-preview cmap-viridis"></div>
        </button>
        <button class="cmap-btn" data-cmap="6" onclick="setCmap(6)">
          Plasma<div class="cmap-preview cmap-plasma"></div>
        </button>
        <button class="cmap-btn" data-cmap="7" onclick="setCmap(7)">
          Turbo<div class="cmap-preview cmap-turbo"></div>
        </button>
      </div>
    </div>

    <div class="card">
      <h2>Brightness & Power</h2>
      <div class="color-picker-wrap">
        <label>Brightness:</label>
        <input type="range" id="brightnessSlider" min="0" max="255" value="255" oninput="setBrightness()">
        <span id="brightnessVal">100%</span>
      </div>
      <div class="color-picker-wrap">
        <label>Max Current:</label>
        <input type="range" id="currentSlider" min="100" max="5000" step="100" value="2000" oninput="setCurrent()">
        <span id="currentVal">2000mA</span>
      </div>
      <div class="color-picker-wrap">
        <label>Button LED:</label>
        <input type="range" id="buttonLedSlider" min="0" max="255" value="255" oninput="setButtonLed()">
        <span id="buttonLedVal">100%</span>
      </div>
    </div>

    <div class="card">
      <button class="mode-btn" style="background:rgba(0,200,100,0.3);border-color:#0c6" onclick="saveAndQuit()">
        Save & Exit
      </button>
    </div>

    <div class="status">
      Connected to WiFi-Spectrum<br>
      Scanning 13 channels
    </div>
  </div>

  <script>
    let state = { mode: 1, cmap: 0, scan: 0, hue: 0, sat: 255, brightness: 255, buttonLed: 255, maxCurrent: 2000 };
    let polling = true;

    // API calls
    async function api(endpoint, params = {}) {
      const url = '/' + endpoint + '?' + new URLSearchParams(params);
      try {
        const r = await fetch(url);
        return r.ok;
      } catch (e) {
        return false;
      }
    }

    async function fetchStatus() {
      try {
        const r = await fetch('/status');
        if (r.ok) {
          const data = await r.json();
          state = { ...state, ...data };
          updateAllUI();
        }
      } catch (e) {}
    }

    // Setters - update server and local state
    async function setMode(mode) {
      state.mode = mode;
      updateAllUI();
      await api('mode', { m: mode });
    }

    async function setCmap(cmap) {
      state.cmap = cmap;
      updateAllUI();
      await api('cmap', { c: cmap });
    }

    async function setScan(scan) {
      state.scan = scan;
      updateAllUI();
      await api('scan', { s: scan });
    }

    async function setColor() {
      state.hue = document.getElementById('hueSlider').value;
      state.sat = document.getElementById('satSlider').value;
      updatePreview();
      await api('color', { h: state.hue, s: state.sat });
    }

    async function setBrightness() {
      state.brightness = document.getElementById('brightnessSlider').value;
      document.getElementById('brightnessVal').textContent = Math.round(state.brightness * 100 / 255) + '%';
      await api('brightness', { b: state.brightness });
    }

    async function setCurrent() {
      state.maxCurrent = document.getElementById('currentSlider').value;
      document.getElementById('currentVal').textContent = state.maxCurrent + 'mA';
      await api('current', { c: state.maxCurrent });
    }

    async function setButtonLed() {
      state.buttonLed = document.getElementById('buttonLedSlider').value;
      document.getElementById('buttonLedVal').textContent = Math.round(state.buttonLed * 100 / 255) + '%';
      await api('buttonled', { b: state.buttonLed });
    }

    // UI updates
    function updateAllUI() {
      // Mode buttons
      document.querySelectorAll('.mode-btn:not(.scan-btn)').forEach(btn => {
        btn.classList.toggle('active', btn.dataset.mode == state.mode);
      });
      // Show/hide sections
      document.getElementById('colorSection').style.display = (state.mode == 0) ? 'block' : 'none';
      document.getElementById('colormapSection').style.display = (state.mode == 1) ? 'block' : 'none';
      // Colormap buttons
      document.querySelectorAll('.cmap-btn').forEach(btn => {
        btn.classList.toggle('active', btn.dataset.cmap == state.cmap);
      });
      // Scan buttons
      document.querySelectorAll('.scan-btn').forEach(btn => {
        btn.classList.toggle('active', btn.dataset.scan == state.scan);
      });
      document.getElementById('scanInfo').textContent =
        state.scan == 0 ? 'Scanning access points (signal strength)' : 'Monitoring packet traffic per channel';
      // Sliders (only if not focused to avoid fighting user input)
      if (document.activeElement.id !== 'hueSlider')
        document.getElementById('hueSlider').value = state.hue;
      if (document.activeElement.id !== 'satSlider')
        document.getElementById('satSlider').value = state.sat;
      if (document.activeElement.id !== 'brightnessSlider') {
        document.getElementById('brightnessSlider').value = state.brightness;
        document.getElementById('brightnessVal').textContent = Math.round(state.brightness * 100 / 255) + '%';
      }
      if (document.activeElement.id !== 'currentSlider') {
        document.getElementById('currentSlider').value = state.maxCurrent;
        document.getElementById('currentVal').textContent = state.maxCurrent + 'mA';
      }
      if (document.activeElement.id !== 'buttonLedSlider') {
        document.getElementById('buttonLedSlider').value = state.buttonLed;
        document.getElementById('buttonLedVal').textContent = Math.round(state.buttonLed * 100 / 255) + '%';
      }
      updatePreview();
    }

    function updatePreview() {
      const hDeg = Math.round(state.hue * 360 / 255);
      const sPct = Math.round(state.sat * 100 / 255);
      document.getElementById('solidPreview').style.background = 'hsl(' + hDeg + ', ' + sPct + '%, 50%)';
    }

    // Save settings and exit AP mode
    async function saveAndQuit() {
      polling = false;  // Stop polling
      await api('quit');
      document.body.innerHTML = '<div style="text-align:center;padding:50px;color:#fff;font-family:sans-serif;"><h1>Settings Saved</h1><p>AP is shutting down.<br>You can close this page.</p></div>';
    }

    // Polling for real-time sync
    async function pollLoop() {
      while (polling) {
        await fetchStatus();
        await new Promise(r => setTimeout(r, 100));
      }
    }

    // Initialize
    fetchStatus().then(() => pollLoop());
  </script>
</body>
</html>
)rawliteral";

// =============================================================================
// Gaussian Kernel
// =============================================================================

void computeGaussianKernel() {
  float ledsPerChannel = (float)NUM_LEDS / NUM_CHANNELS;
  float sigma = 0.5 * ledsPerChannel;

  // Compute Gaussian values
  for (int i = 0; i < KERNEL_SIZE; i++) {
    int x = i - KERNEL_RADIUS;
    gaussianKernel[i] = exp(-(x * x) / (2.0 * sigma * sigma));
  }

  // Normalize so center (peak) = 1.0
  float peak = gaussianKernel[KERNEL_RADIUS];
  for (int i = 0; i < KERNEL_SIZE; i++) {
    gaussianKernel[i] /= peak;
  }

  // Debug print
  Serial.print("Gaussian kernel (sigma=");
  Serial.print(sigma);
  Serial.print("): ");
  for (int i = 0; i < KERNEL_SIZE; i++) {
    Serial.print(gaussianKernel[i], 2);
    Serial.print(" ");
  }
  Serial.println();
}

// Get the LED position (center) for a given channel (0-indexed)
float getChannelLedPosition(int ch) {
  return (float)ch * (NUM_LEDS - 1) / (NUM_CHANNELS - 1);
}

// =============================================================================
// RGB Button LED
// =============================================================================

// LEDC channels for RGB LED PWM
#define LEDC_CHANNEL_R    0
#define LEDC_CHANNEL_G    1
#define LEDC_CHANNEL_B    2
#define LEDC_FREQ         5000
#define LEDC_RESOLUTION   8

void setupButtonLED() {
  ledcAttach(BUTTON_LED_R, LEDC_FREQ, LEDC_RESOLUTION);
  ledcAttach(BUTTON_LED_G, LEDC_FREQ, LEDC_RESOLUTION);
  ledcAttach(BUTTON_LED_B, LEDC_FREQ, LEDC_RESOLUTION);
}

void setButtonLED(uint8_t r, uint8_t g, uint8_t b) {
  // Apply buttonLedBrightness scaling
  uint8_t scaledR = (uint16_t)r * buttonLedBrightness / 255;
  uint8_t scaledG = (uint16_t)g * buttonLedBrightness / 255;
  uint8_t scaledB = (uint16_t)b * buttonLedBrightness / 255;
  ledcWrite(BUTTON_LED_R, scaledR);
  ledcWrite(BUTTON_LED_G, scaledG);
  ledcWrite(BUTTON_LED_B, scaledB);
}

// Update button LED - called from state changes (AP toggle, power toggle)
void updateButtonLED() {
  if (!powerOn) {
    setButtonLED(0, 0, 0);  // Off when powered down
  } else if (apEnabled) {
    setButtonLED(0, 0, 255);  // Blue when AP is on
  }
  // When powered on and AP off, breathing is handled by updateButtonBreathing()
}

// Breathing white effect - call this regularly from loop
void updateButtonBreathing() {
  if (!powerOn || apEnabled) return;  // Only breathe when on and AP off

  // Use lookup table for smooth breathing (period ~2 seconds)
  uint8_t index = (millis() % 2000) * 64 / 2000;
  uint8_t brightness = BREATHING_LUT[index];
  setButtonLED(brightness, brightness, brightness);  // White breathing (R=G=B)
}

// =============================================================================
// Settings Persistence (using Preferences/NVS)
// =============================================================================

void saveSettings() {
  preferences.begin("wifispec", false);  // Read-write mode
  preferences.putUChar("mode", (uint8_t)currentMode);
  preferences.putUChar("cmap", (uint8_t)currentColormap);
  preferences.putUChar("scan", (uint8_t)currentScanMode);
  preferences.putUChar("hue", solidHue);
  preferences.putUChar("sat", solidSat);
  preferences.putUChar("bright", maxBrightness);
  preferences.putUChar("btnLed", buttonLedBrightness);
  preferences.putUShort("maxmA", maxMilliamps);
  preferences.end();
  Serial.println("Settings saved to flash");
}

void loadSettings() {
  preferences.begin("wifispec", true);  // Read-only mode
  currentMode = (DisplayMode)preferences.getUChar("mode", MODE_SOLID_COLOR);
  currentColormap = (Colormap)preferences.getUChar("cmap", CMAP_JET);
  currentScanMode = (ScanMode)preferences.getUChar("scan", SCAN_TRAFFIC);
  solidHue = preferences.getUChar("hue", 64);  // Default pure yellow
  solidSat = preferences.getUChar("sat", 255);
  maxBrightness = preferences.getUChar("bright", 255);
  buttonLedBrightness = preferences.getUChar("btnLed", 255);
  maxMilliamps = preferences.getUShort("maxmA", 2000);
  preferences.end();
  Serial.println("Settings loaded from flash");
}

// =============================================================================
// Setup
// =============================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("=================================");
  Serial.println("  WiFi Spectrum Display v2.0");
  Serial.println("  ESP32-C3 + NeoPixels + Web UI");
  Serial.println("=================================");

  // Initialize button (internal pull-up)
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Initialize RGB button LED (common cathode) with LEDC PWM
  setupButtonLED();
  setButtonLED(0, 255, 0);  // Green on startup

  // Load saved settings from flash
  loadSettings();

  // Compute Gaussian kernel for LED spread
  computeGaussianKernel();

  // Initialize FastLED
  FastLED.addLeds<WS2812B, NEOPIXEL_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(maxBrightness);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, maxMilliamps);
  FastLED.clear();
  FastLED.show();

  // Startup animation
  startupAnimation();

  // Initialize WiFi - start in STA mode then switch to NULL for pure monitor
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_mode(WIFI_MODE_NULL);
  Serial.println("WiFi initialized in NULL mode (pure monitor)");

  // Setup web server routes
  server.on("/", handleRoot);
  server.on("/mode", handleMode);
  server.on("/color", handleColor);
  server.on("/cmap", handleColormap);
  server.on("/scan", handleScanMode);
  server.on("/brightness", handleBrightness);
  server.on("/current", handleCurrent);
  server.on("/buttonled", handleButtonLed);
  server.on("/status", handleStatus);
  server.on("/quit", handleQuit);
  // Captive portal: redirect all unknown requests to main page
  server.onNotFound([]() {
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
  });
  server.begin();

  Serial.println("Web server started at http://192.168.4.1");

  // Setup OTA updates
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);

  ArduinoOTA.onStart([]() {
    FastLED.clear();
    FastLED.show();
    Serial.println("OTA Update starting...");
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA Update complete!");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    int pct = progress / (total / 100);
    int ledProgress = (pct * NUM_LEDS) / 100;
    for (int i = 0; i < NUM_LEDS; i++) {
      leds[i] = (i < ledProgress) ? CRGB::Green : CRGB::Black;
    }
    FastLED.show();
    Serial.printf("OTA Progress: %u%%\r", pct);
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA Error[%u]: ", error);
    for (int i = 0; i < NUM_LEDS; i++) {
      leds[i] = CRGB::Red;
    }
    FastLED.show();
  });

  ArduinoOTA.begin();
  Serial.println("OTA ready (hostname: wifi-spectrum)");

  // Start in traffic mode
  startPromiscuous();
  updateButtonLED();
  Serial.println("Traffic mode active - short press for AP, long press for power");
}

// =============================================================================
// Main loop
// =============================================================================

void powerOff() {
  powerOn = false;
  stopPromiscuous();
  if (apEnabled) disableAP();
  FastLED.clear();
  FastLED.show();
  updateButtonLED();
  Serial.println("Power OFF - long press to wake");
}

void powerOnDevice() {
  powerOn = true;
  startPromiscuous();
  updateButtonLED();
  Serial.println("Power ON");
}

void cycleMode() {
  currentMode = (DisplayMode)((currentMode + 1) % 4);
  updateButtonLED();
  Serial.printf("Mode changed to: %d\n", currentMode);
}

uint8_t pressCount = 0;
unsigned long firstPressTime = 0;

void checkButton() {
  bool rawState = digitalRead(BUTTON_PIN);
  unsigned long now = millis();

  // Debounce: reset timer on state change
  if (rawState != lastButtonState) {
    lastDebounceTime = now;
  }
  lastButtonState = rawState;

  // Only accept state after stable for DEBOUNCE_MS
  if ((now - lastDebounceTime) < DEBOUNCE_MS) {
    return;  // Still bouncing, ignore
  }

  bool prevState = debouncedState;
  debouncedState = rawState;

  // Button just pressed (falling edge)
  if (debouncedState == LOW && prevState == HIGH) {
    buttonPressStart = now;
    buttonHandled = false;
    pressCount++;
    if (pressCount == 1) {
      firstPressTime = now;
    } else if (pressCount >= 2) {
      // Double press detected immediately on second press
      pressCount = 0;
      buttonHandled = true;
      if (powerOn) {
        if (apEnabled) {
          disableAP();
          Serial.println("Double press - AP disabled");
        } else {
          enableAP();
          Serial.println("Double press - AP enabled");
        }
        updateButtonLED();
      }
    }
  }

  // Button being held - check for long press
  if (debouncedState == LOW && !buttonHandled && pressCount > 0) {
    if (now - buttonPressStart >= LONG_PRESS_MS) {
      buttonHandled = true;
      pressCount = 0;
      // Long press: toggle power
      if (powerOn) {
        powerOff();
      } else {
        powerOnDevice();
      }
      Serial.println("Long press - power toggle");
    }
  }

  // Check for single press timeout (double press is handled immediately above)
  if (pressCount == 1 && debouncedState == HIGH && !buttonHandled) {
    if (now - firstPressTime >= DOUBLE_PRESS_MS) {
      // Single press confirmed (no second press came)
      pressCount = 0;
      buttonHandled = true;
      if (powerOn) {
        cycleMode();
        Serial.println("Single press - mode cycle");
      }
    }
  }
}

void loop() {
  server.handleClient();
  ArduinoOTA.handle();
  checkButton();

  // Handle DNS for captive portal when AP is active
  if (apEnabled) {
    dnsServer.processNextRequest();
  }

  // Skip processing if powered off
  if (!powerOn) {
    return;
  }

  unsigned long now = millis();

  // Handle scanning based on mode (skip when AP is active)
  if (!apEnabled) {
    if (currentScanMode == SCAN_RSSI) {
      // RSSI mode: periodic WiFi scans
      if (now - lastScan >= SCAN_DELAY_MS) {
        lastScan = now;
        scanWiFi();
      }
    } else {
      // Traffic mode: channel hopping
      if (now - lastChannelHop >= CHANNEL_HOP_MS) {
        lastChannelHop = now;
        hopChannel();
      }
    }
  }

  // Update animations
  if (now - lastAnimation >= 30) {
    lastAnimation = now;

    if (currentMode == MODE_RAINBOW_SCROLL) {
      rainbowOffset += 2;
    }
    if (currentMode == MODE_CYCLING_COLOR) {
      cyclingHue += 1;
    }

    // Button LED breathing effect
    updateButtonBreathing();
  }

  // Decay LED brightness on its own timer (skip when AP active for preview)
  if (!apEnabled && now - lastDecay >= DECAY_INTERVAL_MS) {
    lastDecay = now;
    decayLEDs();
  }

  // Update max packets scaling every 10 seconds
  if (now - lastMaxReset >= 10000) {
    lastMaxReset = now;
    // Use current period's max, but keep a minimum of 1 to avoid divide-by-zero
    maxPacketsLastSecond = (currentSecondMaxPackets > 0) ? currentSecondMaxPackets : 1;
    currentSecondMaxPackets = 0;
  }

  // Update RSSI scaling every 10 seconds
  if (now - lastRssiReset >= 10000) {
    lastRssiReset = now;
    // Use tracked min/max, with sensible defaults if no data
    if (currentRssiMax > currentRssiMin) {
      rssiMin10s = currentRssiMin;
      rssiMax10s = currentRssiMax;
    }
    // Reset for next period
    currentRssiMin = 0;
    currentRssiMax = -100;
  }

  updateLEDs();
}

// =============================================================================
// WiFi Scanning (RSSI Mode)
// =============================================================================

void scanWiFi() {
  // Reset channel RSSI values
  for (int i = 0; i < NUM_CHANNELS; i++) {
    channelRSSI[i] = RSSI_MIN - 1;
  }

  // Perform WiFi scan (non-blocking would be better but adds complexity)
  int networksFound = WiFi.scanNetworks(false, true, false, 300);

  if (networksFound > 0) {
    for (int i = 0; i < networksFound; i++) {
      int channel = WiFi.channel(i);
      int rssi = WiFi.RSSI(i);

      if (channel >= 1 && channel <= NUM_CHANNELS) {
        if (rssi > channelRSSI[channel - 1]) {
          channelRSSI[channel - 1] = rssi;
        }
        // Track min/max for dynamic scaling
        if (rssi < currentRssiMin) currentRssiMin = rssi;
        if (rssi > currentRssiMax) currentRssiMax = rssi;
      }
    }
  }

  WiFi.scanDelete();

  // Apply Gaussian for all channels after scan
  for (int ch = 0; ch < NUM_CHANNELS; ch++) {
    applyChannelGaussian(ch);
  }
}

// =============================================================================
// Traffic Monitoring (Promiscuous Mode)
// =============================================================================

// Callback for each received packet
void IRAM_ATTR promiscuousCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (currentChannel >= 1 && currentChannel <= NUM_CHANNELS) {
    channelPackets[currentChannel - 1]++;
  }
}

void enableAP() {
  if (apEnabled) return;

  // Stop promiscuous mode - can't do proper scanning while AP is running
  stopPromiscuous();

  // Switch to AP mode using Arduino library
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  // Start DNS server for captive portal (redirect all domains to our IP)
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

  apEnabled = true;
  Serial.printf("AP enabled - connect to %s (192.168.4.1)\n", AP_SSID);
}

void disableAP() {
  if (!apEnabled) return;

  // Save settings to flash before disabling AP
  saveSettings();

  // Stop DNS server
  dnsServer.stop();

  // Disconnect AP and switch to NULL mode for pure monitor
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_mode(WIFI_MODE_NULL);
  apEnabled = false;
  Serial.println("AP disabled - back to NULL mode");

  // Restart promiscuous mode if in traffic scan mode
  if (currentScanMode == SCAN_TRAFFIC) {
    startPromiscuous();
  }
}

void startPromiscuous() {
  if (promiscuousEnabled) return;

  // Reset packet counts
  for (int i = 0; i < NUM_CHANNELS; i++) {
    channelPackets[i] = 0;
    channelPacketsDisplay[i] = 0;
  }

  // Set callback and enable promiscuous mode (no filter = capture all)
  esp_wifi_set_promiscuous_rx_cb(promiscuousCallback);
  esp_wifi_set_promiscuous(true);

  // Set initial channel
  currentChannel = 1;
  esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);

  promiscuousEnabled = true;
  Serial.println("Promiscuous mode enabled");
}

void stopPromiscuous() {
  if (!promiscuousEnabled) return;

  esp_wifi_set_promiscuous(false);
  promiscuousEnabled = false;
  Serial.println("Promiscuous mode disabled");
}

void printChannelStats() {
  for (int i = 0; i < NUM_CHANNELS; i++) {
    Serial.printf("ch%d:%lu", i + 1, channelPacketsDisplay[i]);
    if (i < NUM_CHANNELS - 1) Serial.print(",");
  }
  Serial.println();
}

void printRawPackets() {
  Serial.print("RAW: ");
  for (int i = 0; i < NUM_CHANNELS; i++) {
    Serial.printf("%lu", channelPackets[i]);
    if (i < NUM_CHANNELS - 1) Serial.print(",");
  }
  Serial.println();
}

void hopChannel() {
  // Update display value for the channel we just listened on (before hopping)
  int idx = currentChannel - 1;
  channelPacketsDisplay[idx] = channelPackets[idx];  // Direct assignment

  // Track max packets for dynamic scaling
  if (channelPacketsDisplay[idx] > currentSecondMaxPackets) {
    currentSecondMaxPackets = channelPacketsDisplay[idx];
  }

  channelPackets[idx] = 0;  // Reset for next cycle

  // Apply Gaussian to LEDs immediately for this channel
  applyChannelGaussian(idx);

  // Move to next channel
  currentChannel++;
  if (currentChannel > NUM_CHANNELS) {
    currentChannel = 1;
    // Print stats after each full sweep
    printChannelStats();
  }

  esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
}

// =============================================================================
// Colormap Functions
// =============================================================================

// Helper: interpolate between two RGB colors
CRGB lerpRGB(CRGB a, CRGB b, uint8_t t) {
  return CRGB(
    a.r + ((b.r - a.r) * t >> 8),
    a.g + ((b.g - a.g) * t >> 8),
    a.b + ((b.b - a.b) * t >> 8)
  );
}

// Jet colormap: blue -> cyan -> green -> yellow -> red
CRGB colormapJet(uint8_t v) {
  if (v < 64) {
    return lerpRGB(CRGB(0, 0, 128), CRGB(0, 0, 255), v * 4);
  } else if (v < 128) {
    return lerpRGB(CRGB(0, 0, 255), CRGB(0, 255, 255), (v - 64) * 4);
  } else if (v < 192) {
    return lerpRGB(CRGB(0, 255, 255), CRGB(255, 255, 0), (v - 128) * 4);
  } else {
    return lerpRGB(CRGB(255, 255, 0), CRGB(255, 0, 0), (v - 192) * 4);
  }
}

// HSV colormap: full rainbow
CRGB colormapHSV(uint8_t v) {
  return CHSV(v, 255, 255);
}

// Autumn colormap: red -> orange -> yellow
CRGB colormapAutumn(uint8_t v) {
  return CRGB(255, v, 0);
}

// Hot colormap: black -> red -> yellow -> white
CRGB colormapHot(uint8_t v) {
  if (v < 85) {
    return CRGB(v * 3, 0, 0);
  } else if (v < 170) {
    return CRGB(255, (v - 85) * 3, 0);
  } else {
    return CRGB(255, 255, (v - 170) * 3);
  }
}

// Cool colormap: cyan -> magenta
CRGB colormapCool(uint8_t v) {
  return CRGB(v, 255 - v, 255);
}

// Viridis colormap: purple -> blue -> green -> yellow
CRGB colormapViridis(uint8_t v) {
  // Approximation of viridis
  if (v < 64) {
    return lerpRGB(CRGB(68, 1, 84), CRGB(59, 82, 139), v * 4);
  } else if (v < 128) {
    return lerpRGB(CRGB(59, 82, 139), CRGB(33, 145, 140), (v - 64) * 4);
  } else if (v < 192) {
    return lerpRGB(CRGB(33, 145, 140), CRGB(94, 201, 98), (v - 128) * 4);
  } else {
    return lerpRGB(CRGB(94, 201, 98), CRGB(253, 231, 37), (v - 192) * 4);
  }
}

// Plasma colormap: purple -> pink -> orange -> yellow
CRGB colormapPlasma(uint8_t v) {
  if (v < 64) {
    return lerpRGB(CRGB(13, 8, 135), CRGB(126, 3, 168), v * 4);
  } else if (v < 128) {
    return lerpRGB(CRGB(126, 3, 168), CRGB(204, 71, 120), (v - 64) * 4);
  } else if (v < 192) {
    return lerpRGB(CRGB(204, 71, 120), CRGB(248, 149, 64), (v - 128) * 4);
  } else {
    return lerpRGB(CRGB(248, 149, 64), CRGB(240, 249, 33), (v - 192) * 4);
  }
}

// Turbo colormap: improved rainbow (Google's turbo)
CRGB colormapTurbo(uint8_t v) {
  if (v < 32) {
    return lerpRGB(CRGB(48, 18, 59), CRGB(70, 108, 225), v * 8);
  } else if (v < 96) {
    return lerpRGB(CRGB(70, 108, 225), CRGB(45, 220, 186), (v - 32) * 4);
  } else if (v < 160) {
    return lerpRGB(CRGB(45, 220, 186), CRGB(249, 220, 42), (v - 96) * 4);
  } else if (v < 224) {
    return lerpRGB(CRGB(249, 220, 42), CRGB(229, 80, 14), (v - 160) * 4);
  } else {
    return lerpRGB(CRGB(229, 80, 14), CRGB(122, 4, 3), (v - 224) * 8);
  }
}

// Get color from current colormap
CRGB getColormapColor(uint8_t value) {
  switch (currentColormap) {
    case CMAP_JET:     return colormapJet(value);
    case CMAP_HSV:     return colormapHSV(value);
    case CMAP_AUTUMN:  return colormapAutumn(value);
    case CMAP_HOT:     return colormapHot(value);
    case CMAP_COOL:    return colormapCool(value);
    case CMAP_VIRIDIS: return colormapViridis(value);
    case CMAP_PLASMA:  return colormapPlasma(value);
    case CMAP_TURBO:   return colormapTurbo(value);
    default:           return colormapJet(value);
  }
}

// =============================================================================
// LED Display Functions
// =============================================================================

// Get scaled brightness (0-255) for a channel based on current scan mode
uint8_t getChannelBrightness(int ch) {
  if (currentScanMode == SCAN_RSSI) {
    // RSSI mode - use dynamic min/max from last 10 seconds
    if (channelRSSI[ch] > rssiMin10s && rssiMax10s > rssiMin10s) {
      return map(constrain(channelRSSI[ch], rssiMin10s, rssiMax10s), rssiMin10s, rssiMax10s, 30, 255);
    }
    return 0;
  } else {
    // Traffic mode - map packet count to brightness using dynamic max
    uint32_t packets = channelPacketsDisplay[ch];
    if (packets > 0) {
      return map(constrain(packets, 0, maxPacketsLastSecond), 0, maxPacketsLastSecond, 30, 255);
    }
    return 0;
  }
}

// Apply Gaussian kernel for a single channel directly to LED brightness (instant rise only)
void applyChannelGaussian(int ch) {
  uint8_t brightness = getChannelBrightness(ch);
  if (brightness == 0) return;

  float centerLed = getChannelLedPosition(ch);
  int centerInt = (int)(centerLed + 0.5);

  // Apply kernel centered at this channel's LED position
  for (int k = 0; k < KERNEL_SIZE; k++) {
    int ledIdx = centerInt + (k - KERNEL_RADIUS);
    if (ledIdx >= 0 && ledIdx < NUM_LEDS) {
      uint8_t contribution = (uint8_t)(brightness * gaussianKernel[k]);
      // Instant rise: only set if higher than current
      if (contribution > ledDisplayBrightness[ledIdx]) {
        ledDisplayBrightness[ledIdx] = contribution;
      }
    }
  }
}

// Decay all LED brightness - runs on its own timer
void decayLEDs() {
  for (int led = 0; led < NUM_LEDS; led++) {
    ledDisplayBrightness[led] = (uint8_t)(ledDisplayBrightness[led] * DECAY_FACTOR);
  }
}

// Update LED colors based on current brightness values
void updateLEDs() {
  for (int led = 0; led < NUM_LEDS; led++) {
    uint8_t brightness = ledDisplayBrightness[led];
    CRGB color;

    if (brightness == 0) {
      leds[led] = CRGB::Black;
      continue;
    }

    // Get color at full brightness, then scale uniformly
    switch (currentMode) {
      case MODE_SOLID_COLOR:
        color = CHSV(solidHue, solidSat, 255);
        break;

      case MODE_COLORMAP:
        // When AP active, use brightness as colormap index for preview
        // Otherwise use full color and scale by brightness
        if (apEnabled) {
          color = getColormapColor(brightness);
          leds[led] = color;
          continue;  // Skip brightness scaling for preview
        }
        color = getColormapColor(255);
        break;

      case MODE_RAINBOW_SCROLL:
        {
          uint8_t hue = ((led * 256 / NUM_LEDS) + rainbowOffset) & 0xFF;
          color = CHSV(hue, 255, 255);
        }
        break;

      case MODE_CYCLING_COLOR:
        color = CHSV(cyclingHue, 255, 255);
        break;
    }

    // Apply brightness uniformly to all channels
    color.nscale8(brightness);
    leds[led] = color;
  }

  FastLED.show();
}

// =============================================================================
// Web Server Handlers
// =============================================================================

void handleRoot() {
  server.send(200, "text/html", HTML_PAGE);
}

void handleMode() {
  if (server.hasArg("m")) {
    int mode = server.arg("m").toInt();
    if (mode >= 0 && mode <= 3) {
      currentMode = (DisplayMode)mode;
      updateButtonLED();
      Serial.printf("Mode changed to: %d\n", mode);
    }
  }
  server.send(200, "text/plain", "OK");
}

void handleColor() {
  if (server.hasArg("h")) {
    solidHue = server.arg("h").toInt();
  }
  if (server.hasArg("s")) {
    solidSat = server.arg("s").toInt();
  }
  Serial.printf("Color changed - Hue: %d, Sat: %d\n", solidHue, solidSat);
  server.send(200, "text/plain", "OK");
}

void handleColormap() {
  if (server.hasArg("c")) {
    int cmap = server.arg("c").toInt();
    if (cmap >= 0 && cmap < CMAP_COUNT) {
      currentColormap = (Colormap)cmap;
      // Preview colormap: LED index maps to colormap value 0-255
      for (int i = 0; i < NUM_LEDS; i++) {
        ledDisplayBrightness[i] = map(i, 0, NUM_LEDS - 1, 0, 255);
      }
      Serial.printf("Colormap changed to: %d\n", cmap);
    }
  }
  server.send(200, "text/plain", "OK");
}

void handleScanMode() {
  if (server.hasArg("s")) {
    int scan = server.arg("s").toInt();
    if (scan == SCAN_RSSI && currentScanMode != SCAN_RSSI) {
      stopPromiscuous();
      currentScanMode = SCAN_RSSI;
      Serial.println("Scan mode: RSSI");
    } else if (scan == SCAN_TRAFFIC && currentScanMode != SCAN_TRAFFIC) {
      stopPromiscuous();
      currentScanMode = SCAN_TRAFFIC;
      startPromiscuous();
      Serial.println("Scan mode: Traffic");
    }
  }
  server.send(200, "text/plain", "OK");
}

void handleBrightness() {
  if (server.hasArg("b")) {
    maxBrightness = constrain(server.arg("b").toInt(), 0, 255);
    FastLED.setBrightness(maxBrightness);
    // Show all LEDs at current color/brightness so user can see the effect
    for (int i = 0; i < NUM_LEDS; i++) {
      ledDisplayBrightness[i] = 255;
    }
    Serial.printf("Brightness changed to: %d\n", maxBrightness);
  }
  server.send(200, "text/plain", "OK");
}

void handleCurrent() {
  if (server.hasArg("c")) {
    maxMilliamps = constrain(server.arg("c").toInt(), 100, 5000);
    FastLED.setMaxPowerInVoltsAndMilliamps(5, maxMilliamps);
    Serial.printf("Max current changed to: %d mA\n", maxMilliamps);
  }
  server.send(200, "text/plain", "OK");
}

void handleButtonLed() {
  if (server.hasArg("b")) {
    buttonLedBrightness = constrain(server.arg("b").toInt(), 0, 255);
    Serial.printf("Button LED brightness changed to: %d\n", buttonLedBrightness);
    updateButtonLED();  // Apply immediately
  }
  server.send(200, "text/plain", "OK");
}

void handleStatus() {
  String json = "{\"mode\":" + String(currentMode) +
                ",\"cmap\":" + String(currentColormap) +
                ",\"scan\":" + String(currentScanMode) +
                ",\"ap\":" + String(apEnabled ? "true" : "false") +
                ",\"hue\":" + String(solidHue) +
                ",\"sat\":" + String(solidSat) +
                ",\"brightness\":" + String(maxBrightness) +
                ",\"buttonLed\":" + String(buttonLedBrightness) +
                ",\"maxCurrent\":" + String(maxMilliamps) + "}";
  server.send(200, "application/json", json);
}

void handleQuit() {
  server.send(200, "text/plain", "OK");
  Serial.println("Save & Quit requested");
  delay(100);  // Give time for response to send
  disableAP();
  updateButtonLED();
}

// =============================================================================
// Startup Animation
// =============================================================================

void startupAnimation() {
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CHSV((i * 255) / NUM_LEDS, 255, 200);
    FastLED.show();
    delay(15);
  }

  delay(300);

  for (int b = 200; b >= 0; b -= 10) {
    FastLED.setBrightness(b);
    FastLED.show();
    delay(15);
  }

  FastLED.clear();
  FastLED.setBrightness(maxBrightness);
  FastLED.show();
}
