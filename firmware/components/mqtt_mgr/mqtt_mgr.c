#include "mqtt_mgr.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "catalog.h"
#include "cJSON.h"
#include "list_store.h"
#include "storage_nvs.h"

#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #include "secrets_example.h"
#endif

#define TOPIC_CATALOG_SET    "home/grocery/catalog/set"
#define TOPIC_CATALOG_DELETE "home/grocery/catalog/delete"
#define TOPIC_STATE_PREFIX   "home/grocery/state/"

extern const uint8_t hivemq_ca_pem_start[] asm("_binary_hivemq_ca_pem_start");
extern const uint8_t hivemq_ca_pem_end[]   asm("_binary_hivemq_ca_pem_end");

static const char *TAG = "MQTT_MGR";
static esp_mqtt_client_handle_t s_client = NULL;
static bool s_connected = false;
static list_store_t *s_list_store = NULL;

#define MQTT_DEVICE_NAME  "kitchen-esp32"

static bool parse_category_string(const char *s, category_t *out_cat)
{
    if (!s || !out_cat) return false;

    if (strcmp(s, "Pantry") == 0) {
        *out_cat = CAT_PANTRY;
        return true;
    }
    if (strcmp(s, "Fridge") == 0) {
        *out_cat = CAT_FRIDGE;
        return true;
    }
    if (strcmp(s, "Cleaning") == 0) {
        *out_cat = CAT_CLEANING;
        return true;
    }

    return false;
}

static void handle_catalog_set_message(const char *payload)
{
    cJSON *root = cJSON_Parse(payload);
    if (!root) {
        ESP_LOGW(TAG, "Invalid JSON for catalog/set");
        return;
    }

    cJSON *id_json   = cJSON_GetObjectItemCaseSensitive(root, "id");
    cJSON *code_json = cJSON_GetObjectItemCaseSensitive(root, "code");
    cJSON *name_json = cJSON_GetObjectItemCaseSensitive(root, "name");
    cJSON *cat_json  = cJSON_GetObjectItemCaseSensitive(root, "category");

    if (!cJSON_IsNumber(id_json) ||
        !cJSON_IsString(name_json) ||
        !cJSON_IsString(cat_json)) {
        ESP_LOGW(TAG, "catalog/set missing required fields");
        cJSON_Delete(root);
        return;
    }

    uint8_t id = (uint8_t)id_json->valueint;

    int16_t code = -1;
    if (cJSON_IsNumber(code_json)) {
        code = (int16_t)code_json->valueint;
    }

    category_t cat;
    if (!parse_category_string(cat_json->valuestring, &cat)) {
        ESP_LOGW(TAG, "Invalid category: %s", cat_json->valuestring);
        cJSON_Delete(root);
        return;
    }

    bool ok = catalog_add_or_update_item(id, cat, name_json->valuestring, code);
    if (ok) {
        ESP_LOGI(TAG, "Catalog set OK: id=%u name=%s code=%d",
                 id, name_json->valuestring, code);
    } else {
        ESP_LOGW(TAG, "Catalog set FAILED: id=%u", id);
    }

    cJSON_Delete(root);
}

static void handle_catalog_delete_message(const char *payload)
{
    cJSON *root = cJSON_Parse(payload);
    if (!root) {
        ESP_LOGW(TAG, "Invalid JSON for catalog/delete");
        return;
    }

    cJSON *id_json = cJSON_GetObjectItemCaseSensitive(root, "id");
    cJSON *clear_list_json = cJSON_GetObjectItemCaseSensitive(root, "clear_list");

    if (!cJSON_IsNumber(id_json)) {
        ESP_LOGW(TAG, "catalog/delete missing id");
        cJSON_Delete(root);
        return;
    }

    uint8_t id = (uint8_t)id_json->valueint;

    bool clear_list = true;
    if (cJSON_IsBool(clear_list_json)) {
        clear_list = cJSON_IsTrue(clear_list_json);
    }

    bool ok = catalog_delete_item(id);
    if (ok) {
        ESP_LOGI(TAG, "Catalog delete OK: id=%u clear_list=%s",
                 id, clear_list ? "true" : "false");

        if (clear_list && s_list_store != NULL) {
            bool changed = list_store_set_status(s_list_store, id, ITEM_HAVE);
            if (changed) {
                esp_err_t err = storage_nvs_save_missing_blob(
                    list_store_get_missing_blob(s_list_store),
                    list_store_get_missing_blob_size()
                );
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to save list after catalog delete: %s",
                             esp_err_to_name(err));
                }
            }
        }
    } else {
        ESP_LOGW(TAG, "Catalog delete FAILED: id=%u", id);
    }

    cJSON_Delete(root);
}

static void handle_state_message(const char *topic, const char *payload)
{
    if (strncmp(topic, TOPIC_STATE_PREFIX, strlen(TOPIC_STATE_PREFIX)) != 0) {
        return;
    }

    if (s_list_store == NULL) {
        ESP_LOGW(TAG, "List store not set yet");
        return;
    }

    const char *id_str = topic + strlen(TOPIC_STATE_PREFIX);
    if (!id_str || id_str[0] == '\0') {
        ESP_LOGW(TAG, "State topic missing item id");
        return;
    }

    char *endptr = NULL;
    long id_long = strtol(id_str, &endptr, 10);
    if (endptr == id_str || *endptr != '\0' || id_long < 0 || id_long >= LIST_MAX_ITEMS) {
        ESP_LOGW(TAG, "Invalid item id in topic: %s", topic);
        return;
    }

    uint8_t item_id = (uint8_t)id_long;

    ESP_LOGI(TAG, "State update from MQTT: id=%u payload=%s", item_id, payload);

    item_status_t new_state;
    if (strcmp(payload, "MISSING") == 0) {
        new_state = ITEM_MISSING;
    } else if (strcmp(payload, "HAVE") == 0) {
        new_state = ITEM_HAVE;
    } else {
        ESP_LOGW(TAG, "Unknown state payload: %s", payload);
        return;
    }

    bool changed = list_store_set_status(s_list_store, item_id, new_state);
    if (changed) {
        ESP_LOGI(TAG, "List updated: id=%u", item_id);

        esp_err_t err = storage_nvs_save_missing_blob(
            list_store_get_missing_blob(s_list_store),
            list_store_get_missing_blob_size()
        );
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to save list to NVS: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGI(TAG, "State unchanged for id=%u", item_id);
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        ESP_LOGI(TAG, "Connected ✅");

        esp_mqtt_client_subscribe(event->client, TOPIC_CATALOG_SET, 1);
        esp_mqtt_client_subscribe(event->client, TOPIC_CATALOG_DELETE, 1);
        esp_mqtt_client_subscribe(event->client, "home/grocery/state/+", 1);

        ESP_LOGI(TAG, "Subscribed to catalog and state topics");
        break;

    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG, "Disconnected");
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
        break;

    case MQTT_EVENT_DATA: {
        char topic[event->topic_len + 1];
        char data[event->data_len + 1];

        memcpy(topic, event->topic, event->topic_len);
        topic[event->topic_len] = '\0';

        memcpy(data, event->data, event->data_len);
        data[event->data_len] = '\0';

        ESP_LOGI(TAG, "Received topic=%s data=%s", topic, data);

        if (strcmp(topic, TOPIC_CATALOG_SET) == 0) {
            handle_catalog_set_message(data);
        } else if (strcmp(topic, TOPIC_CATALOG_DELETE) == 0) {
            handle_catalog_delete_message(data);
        } else if (strncmp(topic, TOPIC_STATE_PREFIX, strlen(TOPIC_STATE_PREFIX)) == 0) {
            handle_state_message(topic, data);
        }

        break;
    }

    default:
        break;
    }
}

void mqtt_mgr_set_list_store(list_store_t *store)
{
    s_list_store = store;
}

esp_err_t mqtt_mgr_start(void)
{
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
        .broker.verification.certificate = (const char *)hivemq_ca_pem_start,
        .credentials.username = MQTT_USERNAME,
        .credentials.authentication.password = MQTT_PASSWORD,
        .session.keepalive = 120,
    };

    s_client = esp_mqtt_client_init(&cfg);
    if (!s_client) return ESP_FAIL;

    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_client);

    ESP_LOGI(TAG, "MQTT start requested...");
    return ESP_OK;
}

bool mqtt_mgr_is_connected(void)
{
    return s_connected;
}

esp_err_t mqtt_mgr_publish_item(uint8_t item_id, const char *item_name, const char *status)
{
    if (!s_client || !s_connected) return ESP_ERR_INVALID_STATE;

    char payload[160];
    snprintf(payload, sizeof(payload),
             "{\"id\":%u,\"item\":\"%s\",\"status\":\"%s\"}",
             item_id, item_name, status);

    esp_mqtt_client_publish(s_client, "home/grocery/item/set", payload, 0, 1, 0);

    char topic2[64];
    snprintf(topic2, sizeof(topic2), "home/grocery/state/%u", item_id);
    esp_mqtt_client_publish(s_client, topic2, status, 0, 1, 1);

    ESP_LOGI(TAG, "Published: %s => %s", topic2, status);
    return ESP_OK;
}

esp_err_t mqtt_mgr_publish_full_state(const list_store_t *store)
{
    if (!s_client || !s_connected) return ESP_ERR_INVALID_STATE;
    if (!store) return ESP_ERR_INVALID_ARG;

    for (int i = 0; i < catalog_item_count(); i++) {
        const item_t *it = catalog_get_item(i);
        if (!it) continue;

        const char *status = list_store_is_missing(store, it->id) ? "MISSING" : "HAVE";

        char topic[64];
        snprintf(topic, sizeof(topic), "home/grocery/state/%u", it->id);

        esp_mqtt_client_publish(s_client, topic, status, 0, 1, 1);
        ESP_LOGI(TAG, "Full state publish: %s => %s", topic, status);
    }

    return ESP_OK;
}