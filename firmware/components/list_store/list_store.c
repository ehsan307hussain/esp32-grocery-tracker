#include <stdio.h>
#include <string.h>

#include "list_store.h"
#include "catalog.h"

static inline bool bit_get(const uint32_t *w, uint8_t id)
{
    return (w[id / 32] >> (id % 32)) & 1U;
}

static inline void bit_set(uint32_t *w, uint8_t id, bool val)
{
    uint32_t mask = 1U << (id % 32);
    if (val) w[id / 32] |= mask;
    else     w[id / 32] &= ~mask;
}

void list_store_init(list_store_t *s)
{
    memset(s->missing_words, 0, sizeof(s->missing_words));
}

bool list_store_is_missing(const list_store_t *s, uint8_t item_id)
{
    if (item_id >= LIST_MAX_ITEMS) return false;
    return bit_get(s->missing_words, item_id);
}

bool list_store_set_status(list_store_t *s, uint8_t item_id, item_status_t st)
{
    if (item_id >= LIST_MAX_ITEMS) return false;

    bool was_missing = bit_get(s->missing_words, item_id);
    bool now_missing = (st == ITEM_MISSING);

    bit_set(s->missing_words, item_id, now_missing);
    return was_missing != now_missing;
}

int list_store_first_missing_index(const list_store_t *s)
{
    for (int i = 0; i < catalog_item_count(); i++) {
        const item_t *it = catalog_get_item(i);
        if (it && list_store_is_missing(s, it->id)) return i;
    }
    return -1;
}

int list_store_next_missing_index(const list_store_t *s, int current_index, int direction)
{
    if (catalog_item_count() == 0) return -1;
    int i = current_index;
    for (int tries = 0; tries < catalog_item_count(); tries++) {
        i += direction;
        if (i < 0) i = catalog_item_count() - 1;
        if (i >= catalog_item_count()) i = 0;

        const item_t *it = catalog_get_item(i);
        if (it && list_store_is_missing(s, it->id)) return i;
    }
    return current_index;
}

void list_store_print_missing(const list_store_t *s)
{
    printf("\n===== GROCERY LIST  =====\n");
    int count = 0;
    for (int i = 0; i < catalog_item_count(); i++) {
        const item_t *it = catalog_get_item(i);
        if (it && list_store_is_missing(s, it->id)) {
            printf("- %s\n", it->name);
            count++;
        }
    }
    if (count == 0) printf("(empty)\n");
    printf("=================================\n\n");
}

// ===== New blob-based storage API =====
const void* list_store_get_missing_blob(const list_store_t *s)
{
    return (const void*)s->missing_words;
}

size_t list_store_get_missing_blob_size(void)
{
    return LIST_BYTES;
}

void list_store_set_missing_blob(list_store_t *s, const void *blob, size_t len)
{
    // Zero everything first
    memset(s->missing_words, 0, sizeof(s->missing_words));

    if (!blob || len == 0) return;

    // Copy only what we can fit (handles older smaller blobs safely)
    size_t copy_len = len;
    if (copy_len > sizeof(s->missing_words)) copy_len = sizeof(s->missing_words);

    memcpy(s->missing_words, blob, copy_len);
}

// Backward compatibility helper: old 64-bit into new words
void list_store_set_missing_bits64(list_store_t *s, uint64_t bits64)
{
    memset(s->missing_words, 0, sizeof(s->missing_words));
    // Fill item 0..63
    for (uint8_t id = 0; id < 64 && id < LIST_MAX_ITEMS; id++) {
        bool missing = (bits64 >> id) & 1ULL;
        bit_set(s->missing_words, id, missing);
    }
}