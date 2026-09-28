#include "display.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include <string.h>
#include "driver/gpio.h"
#include "esp_err.h"

// u8g2
#include "u8g2.h"

static const char *TAG = "DISPLAY";

static u8g2_t g_u8g2;
static int g_i2c_port = 0;
static uint8_t g_i2c_addr = 0x3C;

static esp_err_t i2c_write_bytes(const uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (g_i2c_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, (uint8_t*)data, len, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(g_i2c_port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

// u8g2 callback: software i2c byte interface using ESP-IDF I2C master
static uint8_t u8x8_byte_esp32_i2c(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    static uint8_t buffer[128];
    static uint8_t buf_len = 0;

    switch (msg) {
        case U8X8_MSG_BYTE_INIT:
            return 1;

        case U8X8_MSG_BYTE_START_TRANSFER:
            buf_len = 0;
            return 1;

        case U8X8_MSG_BYTE_SEND: {
            uint8_t *data = (uint8_t *)arg_ptr;
            while (arg_int--) {
                if (buf_len < sizeof(buffer)) buffer[buf_len++] = *data++;
            }
            return 1;
        }

        case U8X8_MSG_BYTE_END_TRANSFER: {
            if (buf_len > 0) {
                // u8g2 already includes control bytes (0x00/0x40) in stream
                esp_err_t ret = i2c_write_bytes(buffer, buf_len);
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG, "I2C write failed: %d", ret);
                    return 0;
                }
            }
            return 1;
        }

        default:
            return 0;
    }
}

static uint8_t u8x8_gpio_and_delay_esp32(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    switch (msg) {
        case U8X8_MSG_DELAY_MILLI:
            vTaskDelay(pdMS_TO_TICKS(arg_int));
            return 1;
        default:
            return 0;
    }
}

void display_init(const display_cfg_t *cfg)
{
    g_i2c_port = cfg->i2c_port;
    g_i2c_addr = (uint8_t)cfg->i2c_addr;

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = cfg->pin_sda,
        .scl_io_num = cfg->pin_scl,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,

    };
    ESP_ERROR_CHECK(i2c_param_config(g_i2c_port, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(g_i2c_port, conf.mode, 0, 0, 0));

    // SSD1306 I2C 128x64
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(
        &g_u8g2,
        U8G2_R0,
        u8x8_byte_esp32_i2c,
        u8x8_gpio_and_delay_esp32
    );

    // set address (u8g2 expects 8-bit address)
    u8x8_SetI2CAddress(&g_u8g2.u8x8, g_i2c_addr << 1);

    u8g2_InitDisplay(&g_u8g2);
    u8g2_SetPowerSave(&g_u8g2, 0);

u8g2_SetFont(&g_u8g2, u8g2_font_8x13_tf);



    ESP_LOGI(TAG, "OLED init OK (addr=0x%02X, port=%d)", g_i2c_addr, g_i2c_port);
}

void display_draw_lines(const char *l1, const char *l2, const char *l3, const char *l4)
{
    u8g2_ClearBuffer(&g_u8g2);
   // if (l1) u8g2_DrawStr(&g_u8g2, 0, 12, l1);
    //if (l2) u8g2_DrawStr(&g_u8g2, 0, 28, l2);
    //if (l3) u8g2_DrawStr(&g_u8g2, 0, 44, l3);
    //if (l4) u8g2_DrawStr(&g_u8g2, 0, 60, l4);
    if (l1) u8g2_DrawStr(&g_u8g2, 0, 14, l1);
if (l2) u8g2_DrawStr(&g_u8g2, 0, 30, l2);
if (l3) u8g2_DrawStr(&g_u8g2, 0, 46, l3);
if (l4) u8g2_DrawStr(&g_u8g2, 0, 62, l4);

    u8g2_SendBuffer(&g_u8g2);
}

void display_show_boot(void)
{
    display_draw_lines("Grocery Terminal", "OLED I2C OK", "Keypad ready", "D:Search C:Done");
}
