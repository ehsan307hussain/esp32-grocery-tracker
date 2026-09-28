#pragma once

typedef struct {
    int pin_sda;
    int pin_scl;
    int i2c_port;    // 0 or 1
    int i2c_addr;    // usually 0x3C or 0x3D
} display_cfg_t;

void display_init(const display_cfg_t *cfg);
void display_show_boot(void);
void display_draw_lines(const char *l1, const char *l2, const char *l3, const char *l4);
