#include "storage.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "STORAGE";
static const char *NVS_NAMESPACE = "wifispec";

void storage_init(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

void storage_load_settings(app_settings_t *settings) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Error (%s) opening NVS handle! Loading defaults.", esp_err_to_name(err));
        settings->mode = DEFAULT_MODE;
        settings->cmap = DEFAULT_CMAP;
        settings->scan_mode = DEFAULT_SCAN;
        settings->solid_hue = DEFAULT_HUE;
        settings->solid_sat = DEFAULT_SAT;
        settings->brightness = DEFAULT_BRIGHT;
        settings->button_led_brightness = DEFAULT_BTN_LED;
        settings->max_milliamps = DEFAULT_MAX_MA;
        return;
    }

    uint8_t u8_val;
    uint16_t u16_val;

    if (nvs_get_u8(my_handle, "mode", &u8_val) == ESP_OK) settings->mode = (display_mode_t)u8_val;
    else settings->mode = DEFAULT_MODE;

    if (nvs_get_u8(my_handle, "cmap", &u8_val) == ESP_OK) settings->cmap = (colormap_t)u8_val;
    else settings->cmap = DEFAULT_CMAP;

    if (nvs_get_u8(my_handle, "scan", &u8_val) == ESP_OK) settings->scan_mode = (scan_mode_t)u8_val;
    else settings->scan_mode = DEFAULT_SCAN;

    if (nvs_get_u8(my_handle, "hue", &u8_val) == ESP_OK) settings->solid_hue = u8_val;
    else settings->solid_hue = DEFAULT_HUE;

    if (nvs_get_u8(my_handle, "sat", &u8_val) == ESP_OK) settings->solid_sat = u8_val;
    else settings->solid_sat = DEFAULT_SAT;

    if (nvs_get_u8(my_handle, "bright", &u8_val) == ESP_OK) settings->brightness = u8_val;
    else settings->brightness = DEFAULT_BRIGHT;

    if (nvs_get_u8(my_handle, "btnLed", &u8_val) == ESP_OK) settings->button_led_brightness = u8_val;
    else settings->button_led_brightness = DEFAULT_BTN_LED;

    if (nvs_get_u16(my_handle, "maxmA", &u16_val) == ESP_OK) settings->max_milliamps = u16_val;
    else settings->max_milliamps = DEFAULT_MAX_MA;

    nvs_close(my_handle);
    ESP_LOGI(TAG, "Settings loaded");
}

void storage_save_settings(const app_settings_t *settings) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        return;
    }

    nvs_set_u8(my_handle, "mode", (uint8_t)settings->mode);
    nvs_set_u8(my_handle, "cmap", (uint8_t)settings->cmap);
    nvs_set_u8(my_handle, "scan", (uint8_t)settings->scan_mode);
    nvs_set_u8(my_handle, "hue", settings->solid_hue);
    nvs_set_u8(my_handle, "sat", settings->solid_sat);
    nvs_set_u8(my_handle, "bright", settings->brightness);
    nvs_set_u8(my_handle, "btnLed", settings->button_led_brightness);
    nvs_set_u16(my_handle, "maxmA", settings->max_milliamps);

    err = nvs_commit(my_handle);
    if (err != ESP_OK) ESP_LOGE(TAG, "NVS Commit failed!");
    nvs_close(my_handle);
    ESP_LOGI(TAG, "Settings saved");
}
