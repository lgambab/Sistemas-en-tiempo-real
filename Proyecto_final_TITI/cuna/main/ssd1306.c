
// SSD1306 OLED driver for ESP-IDF (I2C version)
#include "ssd1306.h"
#include "driver/i2c.h"
#include <string.h>

#define SSD1306_I2C_ADDR 0x3C
#define SSD1306_WIDTH    128
#define SSD1306_HEIGHT   64

#define SSD1306_BUFFER_SIZE (SSD1306_WIDTH * SSD1306_HEIGHT / 8)

// SSD1306 Command definitions
#define SSD1306_SET_CONTRAST        0x81
#define SSD1306_SET_ENTIRE_ON       0xA4
#define SSD1306_SET_NORM_INV        0xA6
#define SSD1306_SET_DISP            0xAE
#define SSD1306_SET_MEM_ADDR        0x20
#define SSD1306_SET_COL_ADDR        0x21
#define SSD1306_SET_PAGE_ADDR       0x22
#define SSD1306_SET_DISP_START_LINE 0x40
#define SSD1306_SET_SEG_REMAP       0xA0
#define SSD1306_SET_MUX_RATIO       0xA8
#define SSD1306_SET_COM_OUT_DIR     0xC0
#define SSD1306_SET_DISP_OFFSET     0xD3
#define SSD1306_SET_COM_PIN_CFG     0xDA
#define SSD1306_SET_DISP_CLK_DIV    0xD5
#define SSD1306_SET_PRECHARGE       0xD9
#define SSD1306_SET_VCOM_DESEL      0xDB
#define SSD1306_SET_CHARGE_PUMP     0x8D

static uint8_t ssd1306_buffer[SSD1306_BUFFER_SIZE];

// Helper: send command
static esp_err_t ssd1306_write_cmd(uint8_t cmd) {
    uint8_t data[2] = {0x00, cmd}; // 0x00 = command
    return i2c_master_write_to_device(I2C_NUM_0, SSD1306_I2C_ADDR, data, 2, 100 / portTICK_PERIOD_MS);
}

// Helper: send data
static esp_err_t ssd1306_write_data(const uint8_t *data, size_t len) {
    uint8_t prefix = 0x40; // 0x40 = data
    esp_err_t ret = i2c_master_write_to_device(I2C_NUM_0, SSD1306_I2C_ADDR, &prefix, 1, 100 / portTICK_PERIOD_MS);
    if (ret != ESP_OK) return ret;
    return i2c_master_write_to_device(I2C_NUM_0, SSD1306_I2C_ADDR, data, len, 100 / portTICK_PERIOD_MS);
}

void ssd1306_init(void) {
    // You must initialize I2C elsewhere (not here)
    ssd1306_write_cmd(SSD1306_SET_DISP | 0x00); // Display off
    ssd1306_write_cmd(SSD1306_SET_MEM_ADDR); ssd1306_write_cmd(0x00); // Horizontal addressing
    ssd1306_write_cmd(SSD1306_SET_DISP_START_LINE | 0x00);
    ssd1306_write_cmd(SSD1306_SET_SEG_REMAP | 0x01);
    ssd1306_write_cmd(SSD1306_SET_MUX_RATIO); ssd1306_write_cmd(SSD1306_HEIGHT - 1);
    ssd1306_write_cmd(SSD1306_SET_COM_OUT_DIR | 0x08);
    ssd1306_write_cmd(SSD1306_SET_DISP_OFFSET); ssd1306_write_cmd(0x00);
    ssd1306_write_cmd(SSD1306_SET_COM_PIN_CFG); ssd1306_write_cmd(0x12);
    ssd1306_write_cmd(SSD1306_SET_DISP_CLK_DIV); ssd1306_write_cmd(0x80);
    ssd1306_write_cmd(SSD1306_SET_PRECHARGE); ssd1306_write_cmd(0xF1);
    ssd1306_write_cmd(SSD1306_SET_VCOM_DESEL); ssd1306_write_cmd(0x30);
    ssd1306_write_cmd(SSD1306_SET_CONTRAST); ssd1306_write_cmd(0xFF);
    ssd1306_write_cmd(SSD1306_SET_ENTIRE_ON);
    ssd1306_write_cmd(SSD1306_SET_NORM_INV);
    ssd1306_write_cmd(SSD1306_SET_CHARGE_PUMP); ssd1306_write_cmd(0x14);
    ssd1306_write_cmd(SSD1306_SET_DISP | 0x01); // Display on
    memset(ssd1306_buffer, 0, sizeof(ssd1306_buffer));
    ssd1306_update();
}

void ssd1306_update(void) {
    ssd1306_write_cmd(SSD1306_SET_COL_ADDR);
    ssd1306_write_cmd(0);
    ssd1306_write_cmd(SSD1306_WIDTH - 1);
    ssd1306_write_cmd(SSD1306_SET_PAGE_ADDR);
    ssd1306_write_cmd(0);
    ssd1306_write_cmd((SSD1306_HEIGHT / 8) - 1);
    ssd1306_write_data(ssd1306_buffer, sizeof(ssd1306_buffer));
}

void ssd1306_clear(void) {
    memset(ssd1306_buffer, 0, sizeof(ssd1306_buffer));
    ssd1306_update();
}

void ssd1306_draw_pixel(uint8_t x, uint8_t y, uint8_t color) {
    if (x >= SSD1306_WIDTH || y >= SSD1306_HEIGHT) return;
    if (color)
        ssd1306_buffer[x + (y / 8) * SSD1306_WIDTH] |= (1 << (y % 8));
    else
        ssd1306_buffer[x + (y / 8) * SSD1306_WIDTH] &= ~(1 << (y % 8));
}

// Simple text: draws a single 6x8 font character (ASCII 32-127)
extern const uint8_t font6x8[];
void ssd1306_draw_char(uint8_t x, uint8_t y, char c) {
    if (c < 32 || c > 127) c = '?';
    for (uint8_t i = 0; i < 6; i++) {
        uint8_t line = font6x8[(c - 32) * 6 + i];
        for (uint8_t j = 0; j < 8; j++) {
            ssd1306_draw_pixel(x + i, y + j, (line >> j) & 0x01);
        }
    }
}

void ssd1306_draw_text(uint8_t x, uint8_t y, const char *str) {
    while (*str) {
        ssd1306_draw_char(x, y, *str++);
        x += 6;
    }
}

// Example font (6x8, ASCII 32-127). You must provide this array in ssd1306.h or a separate file.
// ...existing code...
