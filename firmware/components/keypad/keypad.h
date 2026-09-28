#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int rows[4];
    int cols[4];
    int scan_delay_ms;
    int debounce_ms;
} keypad_cfg_t;

void keypad_init(const keypad_cfg_t *cfg);

// returns true if a NEW key press is detected
bool keypad_get_key(char *out_key);
