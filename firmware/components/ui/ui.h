#pragma once
#include "list_store.h"
#include "catalog.h"

typedef enum {
    UI_HOME = 0,
    UI_BROWSE_CAT,
    UI_BROWSE_ITEMS,
    UI_BOUGHT_MODE,
    UI_MESSAGE,
    UI_CODE_ENTRY,
    UI_CODE_CONFIRM
} ui_state_t;

typedef struct {
    ui_state_t state;
    category_t current_cat;
    int current_index;     // index in catalog array
    list_store_t *store;
uint8_t code_digits[2];
uint8_t code_len;
uint8_t selected_id;
    // message timeout
    int msg_ms_left;
    char msg1[32];
    char msg2[32];
} ui_ctx_t;

void ui_init(ui_ctx_t *ui, list_store_t *store);
void ui_handle_key(ui_ctx_t *ui, char key);
void ui_render(ui_ctx_t *ui);
