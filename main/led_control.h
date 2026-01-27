#pragma once

#include "settings.h"
#include <stdint.h>
#include <stdbool.h>

#define NUM_CHANNELS 13
#define NUM_LEDS 24

void led_control_init(void);
void led_control_set_settings(const app_settings_t *settings);
void led_control_update_data(int channel, uint8_t brightness); // Feed brightness (0-255) for a channel
void led_control_loop(void); // Call this periodically (e.g. 30ms or in a task)
void led_control_decay(void); // Call periodically to decay brightness
void led_control_set_preview_mode(bool preview); // If previewing colormap/settings
void led_control_set_preview_values(uint8_t hue, uint8_t sat, uint8_t brightness);
app_settings_t* led_control_get_settings_ptr(void); // Get pointer to live settings

// Helper for other modules if needed
void hsv2rgb(uint8_t h, uint8_t s, uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b);
