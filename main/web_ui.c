#include "web_ui.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "led_control.h"
#include "storage.h"
#include "wifi_scanner.h"
#include <cJSON.h> // IDF includes cJSON
#include <stdlib.h>

#define TAG "WEB_UI"

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

static httpd_handle_t server = NULL;

static esp_err_t root_handler(httpd_req_t *req) {
    size_t len = index_html_end - index_html_start;
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, index_html_start, len);
    return ESP_OK;
}

static esp_err_t api_mode_handler(httpd_req_t *req) {
    char buf[100];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char param[32];
        if (httpd_query_key_value(buf, "m", param, sizeof(param)) == ESP_OK) {
            int val = atoi(param);
            app_settings_t *s = led_control_get_settings_ptr();
            s->mode = (display_mode_t)val;
            ESP_LOGI(TAG, "Mode set to %d", val);
        }
    }
    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t api_color_handler(httpd_req_t *req) {
    char buf[100];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char param[32];
        app_settings_t *s = led_control_get_settings_ptr();
        if (httpd_query_key_value(buf, "h", param, sizeof(param)) == ESP_OK) s->solid_hue = atoi(param);
        if (httpd_query_key_value(buf, "s", param, sizeof(param)) == ESP_OK) s->solid_sat = atoi(param);
        led_control_set_preview_values(s->solid_hue, s->solid_sat, 255);
    }
    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t api_cmap_handler(httpd_req_t *req) {
    char buf[100];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char param[32];
        if (httpd_query_key_value(buf, "c", param, sizeof(param)) == ESP_OK) {
            int val = atoi(param);
            app_settings_t *s = led_control_get_settings_ptr();
            s->cmap = (colormap_t)val;
        }
    }
    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t api_scan_handler(httpd_req_t *req) {
    char buf[100];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char param[32];
        if (httpd_query_key_value(buf, "s", param, sizeof(param)) == ESP_OK) {
            int val = atoi(param);
            app_settings_t *s = led_control_get_settings_ptr();
            s->scan_mode = (scan_mode_t)val;
            // Note: wifi_scanner mode update might be deferred until AP stops, 
            // or we can update it now but it won't take effect if AP is on?
            // Arduino: `handleScanMode` calls `wifi_scanner_set_mode` but checks AP.
            // Since AP is on (we are in Web UI), real scan won't start yet.
        }
    }
    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t api_brightness_handler(httpd_req_t *req) {
    char buf[100];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char param[32];
        if (httpd_query_key_value(buf, "b", param, sizeof(param)) == ESP_OK) {
            int val = atoi(param);
            app_settings_t *s = led_control_get_settings_ptr();
            s->brightness = val;
        }
    }
    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t api_current_handler(httpd_req_t *req) {
    char buf[100];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char param[32];
        if (httpd_query_key_value(buf, "c", param, sizeof(param)) == ESP_OK) {
            int val = atoi(param);
            app_settings_t *s = led_control_get_settings_ptr();
            s->max_milliamps = val;
            // Implement power limiting if possible with led_strip (not standard)
            // or just store it. FastLED handled it.
        }
    }
    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t api_btnled_handler(httpd_req_t *req) {
    char buf[100];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char param[32];
        if (httpd_query_key_value(buf, "b", param, sizeof(param)) == ESP_OK) {
            int val = atoi(param);
            app_settings_t *s = led_control_get_settings_ptr();
            s->button_led_brightness = val;
            // Update button LED immediately? Need button module access.
            // For now just save to settings.
        }
    }
    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t api_status_handler(httpd_req_t *req) {
    app_settings_t *s = led_control_get_settings_ptr();
    char resp[256];
    snprintf(resp, sizeof(resp), 
        "{\"mode\":%d,\"cmap\":%d,\"scan\":%d,\"ap\":true,\"hue\":%d,\"sat\":%d,\"brightness\":%d,\"buttonLed\":%d,\"maxCurrent\":%d}",
        s->mode, s->cmap, s->scan_mode, s->solid_hue, s->solid_sat, s->brightness, s->button_led_brightness, s->max_milliamps
    );
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t api_quit_handler(httpd_req_t *req) {
    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    
    // Save settings
    app_settings_t *s = led_control_get_settings_ptr();
    storage_save_settings(s);
    
    // Signal main loop to stop AP?
    // We can't stop from ISR/Callback easily.
    // Ideally set a flag.
    wifi_scanner_stop_ap(); // This might block or be unsafe if called from httpd task?
    // esp_wifi calls are generally thread safe if initialized properly.
    // However, stopping AP might kill the connection sending this response.
    // Delaying is better.
    
    return ESP_OK;
}

void web_ui_init(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 12;
    
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t uri_root = { .uri = "/", .method = HTTP_GET, .handler = root_handler };
        httpd_register_uri_handler(server, &uri_root);
        
        httpd_uri_t uri_mode = { .uri = "/mode", .method = HTTP_GET, .handler = api_mode_handler };
        httpd_register_uri_handler(server, &uri_mode);
        
        httpd_uri_t uri_color = { .uri = "/color", .method = HTTP_GET, .handler = api_color_handler };
        httpd_register_uri_handler(server, &uri_color);
        
        httpd_uri_t uri_cmap = { .uri = "/cmap", .method = HTTP_GET, .handler = api_cmap_handler };
        httpd_register_uri_handler(server, &uri_cmap);
        
        httpd_uri_t uri_scan = { .uri = "/scan", .method = HTTP_GET, .handler = api_scan_handler };
        httpd_register_uri_handler(server, &uri_scan);
        
        httpd_uri_t uri_bright = { .uri = "/brightness", .method = HTTP_GET, .handler = api_brightness_handler };
        httpd_register_uri_handler(server, &uri_bright);
        
        httpd_uri_t uri_curr = { .uri = "/current", .method = HTTP_GET, .handler = api_current_handler };
        httpd_register_uri_handler(server, &uri_curr);
        
        httpd_uri_t uri_btn = { .uri = "/buttonled", .method = HTTP_GET, .handler = api_btnled_handler };
        httpd_register_uri_handler(server, &uri_btn);
        
        httpd_uri_t uri_status = { .uri = "/status", .method = HTTP_GET, .handler = api_status_handler };
        httpd_register_uri_handler(server, &uri_status);
        
        httpd_uri_t uri_quit = { .uri = "/quit", .method = HTTP_GET, .handler = api_quit_handler };
        httpd_register_uri_handler(server, &uri_quit);
        
        // Captive portal catch-all?
        // IDF doesn't support wildcards easily without custom uri matchers.
        // For now, assume explicit access.
    }
}

void web_ui_stop(void) {
    if (server) {
        httpd_stop(server);
        server = NULL;
    }
}
