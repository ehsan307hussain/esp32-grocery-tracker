#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef enum {
    ITEM_HAVE = 0,
    ITEM_MISSING = 1
} item_status_t;

// ===== Configure max items =====
#define LIST_MAX_ITEMS 100

// Bitset size in 32-bit words (100 -> 4 words -> 128 bits)
#define LIST_WORDS ((LIST_MAX_ITEMS + 31) / 32)
#define LIST_BYTES (LIST_WORDS * sizeof(uint32_t))

typedef struct {
    uint32_t missing_words[LIST_WORDS];
} list_store_t;

void list_store_init(list_store_t *s);

bool list_store_is_missing(const list_store_t *s, uint8_t item_id);

// returns true if state actually changed
bool list_store_set_status(list_store_t *s, uint8_t item_id, item_status_t st);

// find next missing item index in catalog order
int list_store_first_missing_index(const list_store_t *s);
int list_store_next_missing_index(const list_store_t *s, int current_index, int direction);

void list_store_print_missing(const list_store_t *s);

// ===== For NVS storage (new blob-based API) =====
const void* list_store_get_missing_blob(const list_store_t *s);
size_t list_store_get_missing_blob_size(void);
void list_store_set_missing_blob(list_store_t *s, const void *blob, size_t len);

// ===== Optional: backward compatibility helper (old 64-bit) =====
void list_store_set_missing_bits64(list_store_t *s, uint64_t bits64);