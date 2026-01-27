#pragma once

#include "settings.h"
#include <stdint.h>

void wifi_scanner_init(void);
void wifi_scanner_set_mode(scan_mode_t mode);
void wifi_scanner_tick(uint32_t dt_ms); // Call periodically
void wifi_scanner_start_ap(void); // For Web UI
void wifi_scanner_stop_ap(void);

// Data access
uint32_t wifi_scanner_get_packet_count(int channel);
int wifi_scanner_get_rssi(int channel);

// Dynamic scaling
uint32_t wifi_scanner_get_max_packets(void);
int wifi_scanner_get_rssi_min(void);
int wifi_scanner_get_rssi_max(void);
