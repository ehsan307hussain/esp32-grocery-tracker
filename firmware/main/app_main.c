#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>
#include "keypad.h"
#include "display.h"
#include "catalog.h"
#include "list_store.h"
#include "ui.h"
#include "nvs_flash.h"
#include "storage_nvs.h"

#include "wifi_mgr.h"
#include "mqtt_mgr.h"


#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #include "secrets_example.h"
#endif
static const char *TAG = "APP";

void app_main(void)
{
    ESP_LOGI(TAG, "Grocery Terminal starting...");
   
    ESP_ERROR_CHECK(nvs_flash_init());

ESP_ERROR_CHECK(wifi_mgr_init());
ESP_ERROR_CHECK(wifi_mgr_connect_sta(WIFI_SSID, WIFI_PASSWORD, 15000));

if (!catalog_load_from_nvs_or_defaults()) {
    ESP_LOGE(TAG, "Failed to load catalog");
}

list_store_t store;
list_store_init(&store);

// Give MQTT access to the same list store used by the UI
mqtt_mgr_set_list_store(&store);

ESP_ERROR_CHECK(mqtt_mgr_start());
    // Wait for MQTT connection, then publish full current list state once
    for (int tries = 0; tries < 50; tries++) { // about 5 seconds
        if (mqtt_mgr_is_connected()) {
            ESP_LOGI(TAG, "MQTT connected, publishing full grocery list state...");
            ESP_ERROR_CHECK(mqtt_mgr_publish_full_state(&store));
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    //nvs load 
    #include "storage_nvs.h"

// ===== NVS load (new blob format, with migration from old u64) =====
uint8_t blob[LIST_BYTES];
bool migrated = false;

esp_err_t err = storage_nvs_load_missing_blob(blob, sizeof(blob), &migrated);

if (err == ESP_OK) {
    if (migrated) {
        // Old u64 was found; blob contains old u64 in first 8 bytes (per our loader)
        uint64_t old_bits = 0;
        memcpy(&old_bits, blob, sizeof(old_bits));

        // Convert old 0..63 bits into new bitset
        list_store_set_missing_bits64(&store, old_bits);

                ESP_ERROR_CHECK(storage_nvs_save_missing_blob(
            list_store_get_missing_blob(&store),
            list_store_get_missing_blob_size()
        ));
        storage_nvs_erase_old_u64();
    } else {
        // Normal case: blob already contains the new bitset
        list_store_set_missing_blob(&store, blob, sizeof(blob));
    }
} else {
    // If not found, just start empty (OK)
  ESP_LOGW(TAG, "No saved list (err=%s)", esp_err_to_name(err));
}

    // I2C OLED (found at 0x3C)
    display_cfg_t dcfg = {
        .pin_sda = 21,
        .pin_scl = 22,
        .i2c_port = 0,
        .i2c_addr = 0x3C,
    };
    display_init(&dcfg);

    keypad_cfg_t kcfg = {
        .rows = {13,12,14,27},
        .cols = {26,25,33,32},
        .scan_delay_ms = 2,
        .debounce_ms = 80,
    };
    keypad_init(&kcfg);

    ui_ctx_t ui;
    ui_init(&ui, &store);

    display_show_boot();

    while (1) {
        char key = 0;
        if (keypad_get_key(&key)) {
            ESP_LOGI("KEYPAD", "Pressed: %c", key);
            ui_handle_key(&ui, key);
        }

        ui_render(&ui);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
   
}
