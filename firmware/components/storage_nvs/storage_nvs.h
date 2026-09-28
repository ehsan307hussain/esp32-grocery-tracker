#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"


esp_err_t storage_nvs_load_missing_blob(void *out_blob, size_t blob_size, bool *out_migrated);

esp_err_t storage_nvs_save_missing_blob(const void *blob, size_t blob_size);

esp_err_t storage_nvs_erase_old_u64(void);
esp_err_t storage_nvs_save_catalog_blob(const void *blob, size_t blob_size, uint32_t count);
esp_err_t storage_nvs_load_catalog_blob(void *blob, size_t blob_size, uint32_t *out_count);