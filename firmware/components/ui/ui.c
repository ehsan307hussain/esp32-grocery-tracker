#include "ui.h"
#include "display.h"
#include <stdio.h>
#include <string.h>

#include "catalog.h"
#include "list_store.h"
#include "storage_nvs.h"
#include "mqtt_mgr.h"

static void set_message(ui_ctx_t *ui, const char *l1, const char *l2, int ms)
{
    ui->state = UI_MESSAGE;
    snprintf(ui->msg1, sizeof(ui->msg1), "%s", l1 ? l1 : "");
    snprintf(ui->msg2, sizeof(ui->msg2), "%s", l2 ? l2 : "");
    ui->msg_ms_left = ms;
}

static const char* cat_name(category_t c)
{
    switch (c) {
        case CAT_PANTRY: return "Pantry";
        case CAT_FRIDGE: return "Fridge";
        case CAT_CLEANING: return "Cleaning";
        default: return "?";
    }
}

static const item_t* find_item_by_id(uint8_t id)
{
    for (int i = 0; i < catalog_item_count(); i++) {
        const item_t *it = catalog_get_item(i);
        if (it && it->id == id) return it;
    }
    return NULL;
}

void ui_init(ui_ctx_t *ui, list_store_t *store)
{
    memset(ui, 0, sizeof(*ui));
    ui->store = store;
    ui->state = UI_HOME;
    ui->current_cat = CAT_PANTRY;
    ui->current_index = 0;

    ui->code_len = 0;
    ui->selected_id = 0;
}

static void persist_and_publish(list_store_t *store, uint8_t id, const char *name, const char *state_str)
{
    // Persist
    storage_nvs_save_missing_blob(
        list_store_get_missing_blob(store),
        list_store_get_missing_blob_size()
    );

    // Publish (retained state is handled inside mqtt_mgr_publish_item per your design)
    mqtt_mgr_publish_item(id, name, state_str);
}

static void handle_code_complete(ui_ctx_t *ui)
{
    uint8_t code = (uint8_t)(ui->code_digits[0] * 10 + ui->code_digits[1]);
    int item_id = catalog_find_by_code(code);

    if (item_id < 0) {
        ui->code_len = 0;
        set_message(ui, "Invalid code", "Try 00-99", 1200);
        return;
    }

    ui->selected_id = (uint8_t)item_id;
    ui->state = UI_CODE_CONFIRM;
}

void ui_handle_key(ui_ctx_t *ui, char key)
{
    // ===== HOME =====
    if (ui->state == UI_HOME) {
        // Start 2-digit code entry
        if (key >= '0' && key <= '9') {
            ui->state = UI_CODE_ENTRY;
            ui->code_len = 0;
            ui->code_digits[ui->code_len++] = (uint8_t)(key - '0');
            return;
        }

        if (key == 'D') { // Browse (optional)
            ui->state = UI_BROWSE_CAT;
            ui->current_cat = CAT_PANTRY;
            return;
        }

        if (key == 'C') { // Bought mode
            ui->state = UI_BOUGHT_MODE;
            ui->current_index = list_store_first_missing_index(ui->store);
            if (ui->current_index < 0) {
                set_message(ui, "Shopping list", "is empty", 1500);
                ui->state = UI_HOME;
            }
            return;
        }

        return;
    }

    // ===== CODE ENTRY =====
    if (ui->state == UI_CODE_ENTRY) {
        if (key == '#') { // cancel
            ui->code_len = 0;
            ui->state = UI_HOME;
            return;
        }

        if (key == '*') { // backspace
            if (ui->code_len > 0) ui->code_len--;
            if (ui->code_len == 0) ui->state = UI_HOME;
            return;
        }

        if (key >= '0' && key <= '9') {
            if (ui->code_len < 2) {
                ui->code_digits[ui->code_len++] = (uint8_t)(key - '0');
            }

            if (ui->code_len == 2) {
                handle_code_complete(ui);
            }
            return;
        }

        return;
    }

    // ===== CODE CONFIRM =====
    if (ui->state == UI_CODE_CONFIRM) {
        if (key == '#') { // cancel to home
            ui->code_len = 0;
            ui->state = UI_HOME;
            return;
        }

        const item_t *it = find_item_by_id(ui->selected_id);
        if (!it) {
            ui->code_len = 0;
            set_message(ui, "Error", "Item not found", 1200);
            ui->state = UI_HOME;
            return;
        }

        if (key == '*') { // Add as missing
            bool changed = list_store_set_status(ui->store, it->id, ITEM_MISSING);
            if (changed) {
                set_message(ui, "Added:", it->name, 1200);
                list_store_print_missing(ui->store);
                persist_and_publish(ui->store, it->id, it->name, "MISSING");
            } else {
                set_message(ui, "Already on list", it->name, 1200);
            }

            ui->code_len = 0;
            return;
        }

        return;
    }

    // ===== BROWSE CATEGORY =====
    if (ui->state == UI_BROWSE_CAT) {
        if (key == '#') { ui->state = UI_HOME; return; }

        if (key == '1') ui->current_cat = CAT_PANTRY;
        else if (key == '2') ui->current_cat = CAT_FRIDGE;
        else if (key == '3') ui->current_cat = CAT_CLEANING;
        else if (key == '*') {
            ui->state = UI_BROWSE_ITEMS;
            ui->current_index = catalog_first_index_in_category(ui->current_cat);
        }
        return;
    }

    // ===== BROWSE ITEMS =====
    if (ui->state == UI_BROWSE_ITEMS) {
        if (key == '#') { ui->state = UI_BROWSE_CAT; return; }

        if (key == 'A') ui->current_index = catalog_next_index_in_category(ui->current_cat, ui->current_index, -1);
        else if (key == 'B') ui->current_index = catalog_next_index_in_category(ui->current_cat, ui->current_index, +1);
        else if (key == '*') {
            const item_t *it = catalog_get_item(ui->current_index);
            if (!it) return;

            bool changed = list_store_set_status(ui->store, it->id, ITEM_MISSING);
            if (changed) {
                set_message(ui, "Added:", it->name, 1200);
                list_store_print_missing(ui->store);
                persist_and_publish(ui->store, it->id, it->name, "MISSING");
            } else {
                set_message(ui, "Already on list", it->name, 1200);
            }
            ui->state = UI_BROWSE_ITEMS;
        }
        return;
    }

    // ===== BOUGHT MODE =====
    if (ui->state == UI_BOUGHT_MODE) {
        if (key == '#') { ui->state = UI_HOME; return; }
        if (ui->current_index < 0) { ui->state = UI_HOME; return; }

        if (key == 'A') ui->current_index = list_store_next_missing_index(ui->store, ui->current_index, -1);
        else if (key == 'B') ui->current_index = list_store_next_missing_index(ui->store, ui->current_index, +1);
        else if (key == '*') {
            const item_t *it = catalog_get_item(ui->current_index);
            if (!it) return;

            bool changed = list_store_set_status(ui->store, it->id, ITEM_HAVE);
            if (changed) {
                set_message(ui, "Bought", it->name, 1200);
                list_store_print_missing(ui->store);
                persist_and_publish(ui->store, it->id, it->name, "HAVE");
            }

            ui->current_index = list_store_first_missing_index(ui->store);
            if (ui->current_index < 0) ui->state = UI_HOME;
        }
        return;
    }

    // ===== MESSAGE =====
    if (ui->state == UI_MESSAGE) {
        if (key == '#') ui->state = UI_HOME;
        return;
    }
}

void ui_render(ui_ctx_t *ui)
{
    if (ui->state == UI_HOME) {
        display_draw_lines("  Grocery List",
                           "press code 00-99",
                           "C:Bought",
                           "*:Backsp  #:Back");
        return;
    }
    

    if (ui->state == UI_CODE_ENTRY) {
        char buf[8];
        if (ui->code_len == 0) snprintf(buf, sizeof(buf), "__");
        else if (ui->code_len == 1) snprintf(buf, sizeof(buf), "%u_", ui->code_digits[0]);
        else snprintf(buf, sizeof(buf), "%u%u", ui->code_digits[0], ui->code_digits[1]);

        display_draw_lines("ENTER CODE", buf, "*:Backsp", "#:Cancel");
        return;
    }

    if (ui->state == UI_CODE_CONFIRM) {
        const item_t *it = find_item_by_id(ui->selected_id);
        display_draw_lines("CONFIRM",
                           it ? it->name : "?",
                           "*:Add Missing",
                           "#:Cancel");
        return;
    }

    if (ui->state == UI_BROWSE_CAT) {
        char l2[32];
        snprintf(l2, sizeof(l2), "Cat: %s", cat_name(ui->current_cat));
        display_draw_lines("BROWSE CATEGORIES",
                           l2,
                           "1:P 2:F 3:C",
                           "*:Select  #:Back");
        return;
    }

    if (ui->state == UI_BROWSE_ITEMS) {
        const item_t *it = catalog_get_item(ui->current_index);
        display_draw_lines("BROWSE ITEMS",
                           it ? it->name : "(none)",
                           "A:Prev  B:Next",
                           "*:Add   #:Back");
        return;
    }

    if (ui->state == UI_BOUGHT_MODE) {
        const item_t *it = catalog_get_item(ui->current_index);
        display_draw_lines("BOUGHT MODE",
                           it ? it->name : "(empty)",
                           "A:Prev  B:Next",
                           "*:Bought  #:Back");
        return;
    }

    if (ui->state == UI_MESSAGE) {
        display_draw_lines(ui->msg1, ui->msg2, "", "(# to Home)");
        return;
    }
}