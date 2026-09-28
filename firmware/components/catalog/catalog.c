#include "catalog.h"
#include <stddef.h>
#include <string.h>
#include "storage_nvs.h"

typedef struct {
    uint8_t id;
    category_t cat;
    const char *name;
    int16_t code;
} default_item_t;

static item_t g_items[CATALOG_MAX_ITEMS];
static size_t g_item_count = 0;

static const default_item_t DEFAULT_ITEMS[] = {
    {0, CAT_FRIDGE, "Milk", 1},
    {1, CAT_FRIDGE, "Eggs", 2},
    {2, CAT_PANTRY, "Bread", 3},
};

#define DEFAULT_ITEMS_COUNT (sizeof(DEFAULT_ITEMS) / sizeof(DEFAULT_ITEMS[0]))

bool catalog_save_to_nvs(void)
{
    esp_err_t err = storage_nvs_save_catalog_blob(
        g_items,
        sizeof(g_items),
        (uint32_t)g_item_count
    );

    return (err == ESP_OK);
}

void catalog_init_defaults(void)
{
    g_item_count = 0;
    memset(g_items, 0, sizeof(g_items));

    for (size_t i = 0; i < DEFAULT_ITEMS_COUNT; i++) {
        g_items[i].id = DEFAULT_ITEMS[i].id;
        g_items[i].cat = DEFAULT_ITEMS[i].cat;
        strncpy(g_items[i].name, DEFAULT_ITEMS[i].name, sizeof(g_items[i].name) - 1);
        g_items[i].name[sizeof(g_items[i].name) - 1] = '\0';
        g_items[i].code = DEFAULT_ITEMS[i].code;
        g_items[i].active = true;
        g_item_count++;
    }
}

bool catalog_load_from_nvs_or_defaults(void)
{
    uint32_t count = 0;
    esp_err_t err = storage_nvs_load_catalog_blob(
        g_items,
        sizeof(g_items),
        &count
    );

    if (err == ESP_OK && count <= CATALOG_MAX_ITEMS) {
        g_item_count = (size_t)count;
        return true;
    }

    // First boot or load failed: use defaults
    catalog_init_defaults();

    // Save defaults so later boots load from NVS
    if (!catalog_save_to_nvs()) {
        return false;
    }

    return true;
}

int catalog_item_count(void)
{
    return (int)g_item_count;
}

const item_t* catalog_get_item(int index)
{
    if (index < 0 || index >= (int)g_item_count) return NULL;
    if (!g_items[index].active) return NULL;
    return &g_items[index];
}

int catalog_find_by_code(uint8_t code)
{
    for (int i = 0; i < (int)g_item_count; i++) {
        if (g_items[i].active && g_items[i].code >= 0 && (uint8_t)g_items[i].code == code) {
            return g_items[i].id;
        }
    }
    return -1;
}


int catalog_find_by_id(uint8_t id)
{
    for (int i = 0; i < (int)g_item_count; i++) {
        if (g_items[i].id == id) {
            return i;
        }
    }
    return -1;
}

int catalog_first_index_in_category(category_t cat)
{
    for (int i = 0; i < (int)g_item_count; i++) {
        if (g_items[i].active && g_items[i].cat == cat) return i;
    }
    return -1;
}

int catalog_next_index_in_category(category_t cat, int current_index, int direction)
{
    if (g_item_count == 0) return -1;
    if (current_index < 0) return catalog_first_index_in_category(cat);

    int i = current_index;

    while (1) {
        i += direction;

        if (i < 0) i = (int)g_item_count - 1;
        if (i >= (int)g_item_count) i = 0;

        if (g_items[i].active && g_items[i].cat == cat) return i;
        if (i == current_index) break;
    }

    return current_index;
}
bool catalog_code_exists(int16_t code, uint8_t exclude_id)
{
    if (code < 0) return false;  // no-code is always allowed

    for (int i = 0; i < (int)g_item_count; i++) {
        if (!g_items[i].active) continue;

        if (g_items[i].id == exclude_id) continue;

        if (g_items[i].code == code) {
            return true;
        }
    }

    return false;
}
bool catalog_add_or_update_item(uint8_t id, category_t cat, const char *name, int16_t code)
{
    if (name == NULL || name[0] == '\0') {
        return false;
    }

    if (cat < 0 || cat >= CAT_COUNT) {
        return false;
    }

    if (catalog_code_exists(code, id)) {
        return false;   // duplicate code
    }

    int idx = catalog_find_by_id(id);

    // Update existing item
    if (idx >= 0) {
        g_items[idx].cat = cat;
        strncpy(g_items[idx].name, name, sizeof(g_items[idx].name) - 1);
        g_items[idx].name[sizeof(g_items[idx].name) - 1] = '\0';
        g_items[idx].code = code;
        g_items[idx].active = true;

        return catalog_save_to_nvs();
    }

    // Add new item
    if (g_item_count >= CATALOG_MAX_ITEMS) {
        return false;
    }

    g_items[g_item_count].id = id;
    g_items[g_item_count].cat = cat;
    strncpy(g_items[g_item_count].name, name, sizeof(g_items[g_item_count].name) - 1);
    g_items[g_item_count].name[sizeof(g_items[g_item_count].name) - 1] = '\0';
    g_items[g_item_count].code = code;
    g_items[g_item_count].active = true;

    g_item_count++;

    return catalog_save_to_nvs();
}
bool catalog_archive_item(uint8_t id)
{
    int idx = catalog_find_by_id(id);
    if (idx < 0) {
        return false;
    }

    g_items[idx].active = false;
    g_items[idx].code = -1;

    return catalog_save_to_nvs();
}
bool catalog_delete_item(uint8_t id)
{
    int idx = catalog_find_by_id(id);
    if (idx < 0) {
        return false;
    }

    // Shift all items left to remove this one
    for (int i = idx; i < (int)g_item_count - 1; i++) {
        g_items[i] = g_items[i + 1];
    }

    // Clear last slot
    memset(&g_items[g_item_count - 1], 0, sizeof(item_t));

    g_item_count--;

    return catalog_save_to_nvs();
}