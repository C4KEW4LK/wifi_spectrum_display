#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "storage.h"
#include "led_control.h"
#include "wifi_scanner.h"
#include "web_ui.h"
#include "button.h"
#include "settings.h"
#include "dns_server.h"

#define TAG "APP"

static bool power_on = true;
static bool ap_enabled = false;

static void handle_button_event(button_event_t event) {
    app_settings_t *s = led_control_get_settings_ptr();

    switch (event) {
        case BTN_PRESS_SINGLE:
            if (power_on && !ap_enabled) {
                s->mode = (display_mode_t)((s->mode + 1) % 4);
                ESP_LOGI(TAG, "Mode cycled to %d", s->mode);
                // Not saving to NVS to avoid wear, explicit save via UI
            }
            break;
            
        case BTN_PRESS_DOUBLE:
            if (power_on) {
                if (ap_enabled) {
                    wifi_scanner_stop_ap();
                    web_ui_stop();
                    dns_server_stop();
                    ap_enabled = false;
                    led_control_set_preview_mode(false);
                    button_set_led(0, 0, 0); 
                } else {
                    wifi_scanner_start_ap();
                    dns_server_start();
                    web_ui_init();
                    ap_enabled = true;
                    led_control_set_preview_mode(true);
                    button_set_led(0, 0, 255); // Blue for AP
                }
            }
            break;
            
        case BTN_PRESS_LONG:
            power_on = !power_on;
            if (!power_on) {
                if (ap_enabled) {
                    wifi_scanner_stop_ap();
                    web_ui_stop();
                    dns_server_stop();
                    ap_enabled = false;
                    led_control_set_preview_mode(false);
                }
                ESP_LOGI(TAG, "Power OFF");
                button_set_led(0, 0, 0);
                // Clear LEDs once
                for(int i=0; i<NUM_LEDS; i++) led_control_update_data(i, 0);
                led_control_decay(); // Force decay/clear
                led_control_loop();  // Push clear
            } else {
                ESP_LOGI(TAG, "Power ON");
            }
            break;
    }
}

void app_main(void)
{
    // Init Storage
    storage_init();
    app_settings_t settings;
    storage_load_settings(&settings);
    
    // Init LED
    led_control_init();
    led_control_set_settings(&settings);
    
    // Init Button
    button_init();
    button_set_callback(handle_button_event);
    button_set_led(0, 255, 0); // Green start

    // Init WiFi
    wifi_scanner_init();
    wifi_scanner_set_mode(settings.scan_mode);

    // Main Loop
    const uint32_t loop_period = 30; // 30ms ~ 33Hz
    
    while (1) {
        // Tick components
        button_tick(loop_period);
        
        if (power_on) {
            wifi_scanner_tick(loop_period);
            
            // Feed data to LEDs if not in AP preview mode
            if (!ap_enabled) {
                // Decay old values
                led_control_decay();
                
                // Get new values
                if (settings.scan_mode == SCAN_TRAFFIC) {
                    // Feed all channels
                    for (int i = 0; i < NUM_CHANNELS; i++) {
                        uint32_t pkts = wifi_scanner_get_packet_count(i + 1);
                        uint32_t max_pkts = wifi_scanner_get_max_packets();
                        if (pkts > 0 && max_pkts > 0) {
                            // Map to 30-255
                            uint8_t val = 30 + (pkts * 225 / max_pkts);
                            led_control_update_data(i, val);
                        }
                    }
                } else {
                    // RSSI Mode
                    int min_rssi = wifi_scanner_get_rssi_min();
                    int max_rssi = wifi_scanner_get_rssi_max();
                    
                    for (int i = 0; i < NUM_CHANNELS; i++) {
                        int rssi = wifi_scanner_get_rssi(i + 1);
                        if (rssi > min_rssi) {
                            // Map RSSI to 30-255
                            int val = 30 + ((rssi - min_rssi) * 225) / (max_rssi - min_rssi);
                            if (val > 255) val = 255;
                            if (val < 0) val = 0;
                            led_control_update_data(i, (uint8_t)val);
                        }
                    }
                }
                
                button_update_breathing(loop_period);
                led_control_loop(); // Render
            } else {
                // AP Mode: Button LED Blue
                 button_set_led(0, 0, 255);
                 led_control_loop(); // Keep rendering for preview/menu effects
            }
        } else {
             // Power off - do nothing, maybe sleep?
             vTaskDelay(pdMS_TO_TICKS(100));
        }

        vTaskDelay(pdMS_TO_TICKS(loop_period));
    }
}
