#pragma once
#include <stdint.h>
#include <stdbool.h>

#define CATALOG_MAX_ITEMS 100
#define CATALOG_NAME_LEN  24

typedef enum {
    CAT_PANTRY = 0,
    CAT_FRIDGE = 1,
    CAT_CLEANING = 2,
    CAT_COUNT
} category_t;

typedef struct {
    uint8_t id;
    category_t cat;
    char name[24];
    int16_t code;
    bool active;
} item_t;

void catalog_init_defaults(void);

int catalog_item_count(void);
const item_t* catalog_get_item(int index);

// quick key -> item id, returns -1 if none
int catalog_find_by_quick_key(char key);

// for browse lists
int catalog_first_index_in_category(category_t cat);
int catalog_next_index_in_category(category_t cat, int current_index, int direction); // direction: +1 / -1
int catalog_find_by_code(uint8_t code);
bool catalog_load_from_nvs_or_defaults(void);
bool catalog_save_to_nvs(void);

bool catalog_add_or_update_item(uint8_t id, category_t cat, const char *name, int16_t code);
bool catalog_archive_item(uint8_t id);
bool catalog_code_exists(int16_t code, uint8_t exclude_id);
int catalog_find_by_id(uint8_t id);
bool catalog_delete_item(uint8_t id);