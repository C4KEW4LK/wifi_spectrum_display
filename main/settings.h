#pragma once

#include <stdint.h>

typedef enum {
    MODE_SOLID_COLOR = 0,
    MODE_COLORMAP,
    MODE_RAINBOW_SCROLL,
    MODE_CYCLING_COLOR
} display_mode_t;

typedef enum {
    CMAP_JET = 0,
    CMAP_HSV,
    CMAP_AUTUMN,
    CMAP_HOT,
    CMAP_COOL,
    CMAP_VIRIDIS,
    CMAP_PLASMA,
    CMAP_TURBO,
    CMAP_COUNT
} colormap_t;

typedef enum {
    SCAN_RSSI = 0,
    SCAN_TRAFFIC
} scan_mode_t;

typedef struct {
    display_mode_t mode;
    colormap_t cmap;
    scan_mode_t scan_mode;
    uint8_t solid_hue;
    uint8_t solid_sat;
    uint8_t brightness;
    uint8_t button_led_brightness;
    uint16_t max_milliamps;
} app_settings_t;

// Defaults
#define DEFAULT_MODE MODE_COLORMAP
#define DEFAULT_CMAP CMAP_JET
#define DEFAULT_SCAN SCAN_TRAFFIC
#define DEFAULT_HUE 64
#define DEFAULT_SAT 255
#define DEFAULT_BRIGHT 255
#define DEFAULT_BTN_LED 255
#define DEFAULT_MAX_MA 2000
