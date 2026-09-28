#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "list_store.h"

void mqtt_mgr_set_list_store(list_store_t *store);

esp_err_t mqtt_mgr_start(void);
bool mqtt_mgr_is_connected(void);

// Publish both:
// - event JSON to home/grocery/item/set (retain=0)
// - retained state to home/grocery/state/<id> (retain=1)
esp_err_t mqtt_mgr_publish_item(uint8_t item_id, const char *item_name, const char *status);
esp_err_t mqtt_mgr_publish_full_state(const list_store_t *store);