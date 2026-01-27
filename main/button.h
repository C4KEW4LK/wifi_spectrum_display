#pragma once

#include <stdint.h>
#include <stdbool.h>

void button_init(void);
void button_tick(uint32_t dt_ms);
void button_set_led(uint8_t r, uint8_t g, uint8_t b);
void button_update_breathing(uint32_t dt_ms);

// Callbacks
typedef enum {
    BTN_PRESS_SINGLE,
    BTN_PRESS_DOUBLE,
    BTN_PRESS_LONG
} button_event_t;

typedef void (*button_callback_t)(button_event_t event);
void button_set_callback(button_callback_t cb);
