#include "keypad.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static keypad_cfg_t g_cfg;

static const char KEYMAP[4][4] = {
    {'1','2','3','A'},
    {'4','5','6','B'},
    {'7','8','9','C'},
    {'*','0','#','D'}
};

static int64_t g_last_press_us = 0;
static char g_last_key = 0;

void keypad_init(const keypad_cfg_t *cfg)
{
    g_cfg = *cfg;

    // rows outputs, default HIGH
    for (int r = 0; r < 4; r++) {
        gpio_config_t io = {
            .pin_bit_mask = (1ULL << g_cfg.rows[r]),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        gpio_config(&io);
        gpio_set_level(g_cfg.rows[r], 1);
    }

    // cols inputs with pull-ups
    for (int c = 0; c < 4; c++) {
        gpio_config_t io = {
            .pin_bit_mask = (1ULL << g_cfg.cols[c]),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        gpio_config(&io);
    }
}

static bool scan_matrix(char *out_key)
{
    for (int r = 0; r < 4; r++) {
        // drive all rows HIGH, then pull current row LOW
        for (int rr = 0; rr < 4; rr++) gpio_set_level(g_cfg.rows[rr], 1);
        gpio_set_level(g_cfg.rows[r], 0);

        vTaskDelay(pdMS_TO_TICKS(g_cfg.scan_delay_ms));

        for (int c = 0; c < 4; c++) {
            int level = gpio_get_level(g_cfg.cols[c]);
            if (level == 0) { // active low
                *out_key = KEYMAP[r][c];
                return true;
            }
        }
    }
    return false;
}

bool keypad_get_key(char *out_key)
{
    char key = 0;
    if (!scan_matrix(&key)) {
        g_last_key = 0; // released
        return false;
    }

    int64_t now = esp_timer_get_time();
    int64_t debounce_us = (int64_t)g_cfg.debounce_ms * 1000;

    // Only report when key is newly pressed (was released)
    if (key != 0 && g_last_key == 0) {
        if (now - g_last_press_us >= debounce_us) {
            g_last_press_us = now;
            g_last_key = key;
            *out_key = key;
            return true;
        }
    }

    return false;
}
