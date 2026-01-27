#include "wifi_scanner.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "led_control.h" // For NUM_CHANNELS
#include <string.h>

#define TAG "WIFI_SCAN"

static scan_mode_t current_mode = SCAN_TRAFFIC;
static bool ap_enabled = false;
static bool promiscuous_enabled = false;
static bool scan_in_progress = false;

// Packet counting
static volatile uint32_t channel_packets[NUM_CHANNELS];
static uint32_t channel_packets_display[NUM_CHANNELS];
static int channel_rssi[NUM_CHANNELS];

// State
static int current_channel = 1;
static uint32_t time_since_hop = 0;
static uint32_t time_since_scan = 0;

// Scaling
static uint32_t current_second_max_packets = 0;
static uint32_t max_packets_last_second = 1;
static uint32_t time_since_scale_reset = 0;

// RSSI Scaling
static int rssi_min = -90;
static int rssi_max = -30;

// Settings
#define CHANNEL_HOP_MS 5
#define SCAN_DELAY_MS 10

static void IRAM_ATTR promiscuous_rx_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
    if (current_channel >= 1 && current_channel <= NUM_CHANNELS) {
        channel_packets[current_channel - 1]++;
    }
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        scan_in_progress = false;
        
        uint16_t ap_count = 0;
        esp_wifi_scan_get_ap_num(&ap_count);
        
        if (ap_count > 0) {
            wifi_ap_record_t *ap_list = (wifi_ap_record_t *)malloc(ap_count * sizeof(wifi_ap_record_t));
            if (ap_list) {
                if (esp_wifi_scan_get_ap_records(&ap_count, ap_list) == ESP_OK) {
                    // Reset RSSI
                    for (int i=0; i<NUM_CHANNELS; i++) channel_rssi[i] = -95;
                    
                    // Process results
                    for (int i = 0; i < ap_count; i++) {
                        int ch = ap_list[i].primary;
                        if (ch >= 1 && ch <= NUM_CHANNELS) {
                            if (ap_list[i].rssi > channel_rssi[ch-1]) {
                                channel_rssi[ch-1] = ap_list[i].rssi;
                            }
                        }
                    }
                }
                free(ap_list);
            }
        }
    }
}

void wifi_scanner_init(void) {
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    // Init RSSI array
    for (int i=0; i<NUM_CHANNELS; i++) channel_rssi[i] = -95;
}

void wifi_scanner_set_mode(scan_mode_t mode) {
    current_mode = mode;
    if (mode == SCAN_TRAFFIC && !ap_enabled) {
        if (!promiscuous_enabled) {
            // Ensure we are in STA/NULL mode for promiscuous if needed? 
            // ESP-IDF allows promiscuous in STA/AP modes too, but NULL is cleanest.
            if (scan_in_progress) {
                // Should stop scan?
            }
            esp_wifi_set_mode(WIFI_MODE_NULL); 
            esp_wifi_set_promiscuous_rx_cb(promiscuous_rx_cb);
            esp_wifi_set_promiscuous(true);
            promiscuous_enabled = true;
            current_channel = 1;
            esp_wifi_set_channel(current_channel, WIFI_SECOND_CHAN_NONE);
        }
    } else {
        if (promiscuous_enabled) {
            esp_wifi_set_promiscuous(false);
            promiscuous_enabled = false;
        }
        if (mode == SCAN_RSSI) {
            esp_wifi_set_mode(WIFI_MODE_STA);
        }
    }
}

void wifi_scanner_start_ap(void) {
    if (ap_enabled) return;
    
    wifi_scanner_set_mode(SCAN_RSSI); // Stop promiscuous, ensure STA mode logic handled below
    
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = "WiFi-Spectrum",
            .ssid_len = strlen("WiFi-Spectrum"),
            .password = "spectrum123",
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
            .channel = 1
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ap_enabled = true;
    ESP_LOGI(TAG, "AP Enabled");
}

void wifi_scanner_stop_ap(void) {
    if (!ap_enabled) return;
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
    ap_enabled = false;
    ESP_LOGI(TAG, "AP Disabled");
    
    // Restore mode
    wifi_scanner_set_mode(current_mode);
}

void wifi_scanner_tick(uint32_t dt_ms) {
    if (ap_enabled) return;

    if (current_mode == SCAN_TRAFFIC) {
        time_since_hop += dt_ms;
        if (time_since_hop >= CHANNEL_HOP_MS) {
            time_since_hop = 0;
            
            // Latch data
            int idx = current_channel - 1;
            channel_packets_display[idx] = channel_packets[idx];
            if (channel_packets_display[idx] > current_second_max_packets) {
                current_second_max_packets = channel_packets_display[idx];
            }
            channel_packets[idx] = 0;

            // Hop
            current_channel++;
            if (current_channel > NUM_CHANNELS) current_channel = 1;
            esp_wifi_set_channel(current_channel, WIFI_SECOND_CHAN_NONE);
        }
        
        // Scale reset
        time_since_scale_reset += dt_ms;
        if (time_since_scale_reset >= 1000) {
            time_since_scale_reset = 0;
            max_packets_last_second = (current_second_max_packets > 0) ? current_second_max_packets : 1;
            current_second_max_packets = 0;
        }
        
    } else {
        // RSSI Scan
        time_since_scan += dt_ms;
        if (!scan_in_progress && time_since_scan >= SCAN_DELAY_MS) {
            time_since_scan = 0;
            
            wifi_scan_config_t scan_config = {
                .show_hidden = true,
                .scan_type = WIFI_SCAN_TYPE_ACTIVE,
                .scan_time.active.min = 50,
                .scan_time.active.max = 100,
            };
            
            if (esp_wifi_scan_start(&scan_config, false) == ESP_OK) {
                scan_in_progress = true;
            }
        }
    }
}

uint32_t wifi_scanner_get_packet_count(int channel) {
    if (channel < 1 || channel > NUM_CHANNELS) return 0;
    return channel_packets_display[channel - 1];
}

uint32_t wifi_scanner_get_max_packets(void) {
    return max_packets_last_second;
}

int wifi_scanner_get_rssi(int channel) {
    if (channel < 1 || channel > NUM_CHANNELS) return -95;
    return channel_rssi[channel - 1];
}

int wifi_scanner_get_rssi_min(void) { return rssi_min; }
int wifi_scanner_get_rssi_max(void) { return rssi_max; }