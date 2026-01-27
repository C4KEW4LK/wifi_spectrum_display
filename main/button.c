#include "button.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "led_control.h" // For settings access if needed for brightness

#define BUTTON_PIN GPIO_NUM_9
#define BTN_LED_G GPIO_NUM_3
#define BTN_LED_B GPIO_NUM_2
#define BTN_LED_R GPIO_NUM_1

// Timing
#define DEBOUNCE_MS 50
#define DOUBLE_PRESS_MS 400
#define LONG_PRESS_MS 3000

// LEDC
#define LEDC_TIMER LEDC_TIMER_0
#define LEDC_MODE LEDC_LOW_SPEED_MODE
#define LEDC_RES LEDC_TIMER_8_BIT
#define LEDC_FREQ 5000

static button_callback_t event_cb = NULL;
static bool last_state = true; // High (pullup)
static bool debounced_state = true;
static uint32_t debounce_time = 0;
static uint32_t press_start_time = 0;
static int press_count = 0;
static uint32_t first_press_time = 0;
static bool button_handled = false;

// Breathing
static uint32_t breathing_time = 0;
static const uint8_t BREATHING_LUT[64] = {
  128, 140, 152, 165, 176, 188, 198, 208,
  218, 226, 234, 240, 245, 250, 253, 255,
  255, 255, 253, 250, 245, 240, 234, 226,
  218, 208, 198, 188, 176, 165, 152, 140,
  128, 115, 103,  90,  79,  67,  57,  47,
   37,  29,  21,  15,  10,   5,   2,   0,
    0,   0,   2,   5,  10,  15,  21,  29,
   37,  47,  57,  67,  79,  90, 103, 115
};

void button_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    // LEDC Init
    ledc_timer_config_t timer_conf = {
        .speed_mode = LEDC_MODE,
        .timer_num = LEDC_TIMER,
        .duty_resolution = LEDC_RES,
        .freq_hz = LEDC_FREQ,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&timer_conf);

    ledc_channel_config_t chan_conf = {
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = BTN_LED_R,
        .duty = 0,
        .hpoint = 0
    };
    ledc_channel_config(&chan_conf);
    
    chan_conf.channel = LEDC_CHANNEL_1;
    chan_conf.gpio_num = BTN_LED_G;
    ledc_channel_config(&chan_conf);
    
    chan_conf.channel = LEDC_CHANNEL_2;
    chan_conf.gpio_num = BTN_LED_B;
    ledc_channel_config(&chan_conf);
}

void button_set_callback(button_callback_t cb) {
    event_cb = cb;
}

void button_set_led(uint8_t r, uint8_t g, uint8_t b) {
    app_settings_t *s = led_control_get_settings_ptr();
    uint8_t brightness = s->button_led_brightness;
    
    uint32_t duty_r = (r * brightness) / 255;
    uint32_t duty_g = (g * brightness) / 255;
    uint32_t duty_b = (b * brightness) / 255;

    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_0, duty_r);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_0);
    
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_1, duty_g);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_1);
    
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_2, duty_b);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_2);
}

void button_update_breathing(uint32_t dt_ms) {
    breathing_time += dt_ms;
    uint8_t index = (breathing_time % 2000) * 64 / 2000;
    uint8_t val = BREATHING_LUT[index];
    button_set_led(val, val, val);
}

void button_tick(uint32_t dt_ms) {
    bool raw = gpio_get_level(BUTTON_PIN);
    
    if (raw != last_state) {
        debounce_time = 0; // Reset debounce timer
    }
    last_state = raw;
    debounce_time += dt_ms;
    
    if (debounce_time < DEBOUNCE_MS) return;
    
    bool prev_debounced = debounced_state;
    debounced_state = raw;
    
    // Falling edge (Press)
    if (prev_debounced == true && debounced_state == false) {
        press_start_time = 0; // Relative to start of press
        button_handled = false;
        press_count++;
        if (press_count == 1) first_press_time = 0; // Relative tracker
        
        if (press_count >= 2) {
            if (event_cb) event_cb(BTN_PRESS_DOUBLE);
            press_count = 0;
            button_handled = true;
        }
    }
    
    // Holding
    if (debounced_state == false) {
        press_start_time += dt_ms;
        if (!button_handled && press_count > 0 && press_start_time >= LONG_PRESS_MS) {
            if (event_cb) event_cb(BTN_PRESS_LONG);
            button_handled = true;
            press_count = 0;
        }
    } else {
        // Released
        if (press_count == 1 && !button_handled) {
            first_press_time += dt_ms;
            if (first_press_time >= DOUBLE_PRESS_MS) {
                if (event_cb) event_cb(BTN_PRESS_SINGLE);
                press_count = 0;
                button_handled = true;
            }
        }
    }
}
