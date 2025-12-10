#ifndef SSD1306_H
#define SSD1306_H

#include <stdint.h>
#include "esp_err.h"

#define SSD1306_WIDTH    128
#define SSD1306_HEIGHT   64

void ssd1306_init(void);
void ssd1306_update(void);
void ssd1306_clear(void);
void ssd1306_fill(uint8_t pattern);
void ssd1306_draw_pixel(uint8_t x, uint8_t y, uint8_t color);
void ssd1306_draw_char(uint8_t x, uint8_t y, char c);
void ssd1306_draw_text(uint8_t x, uint8_t y, const char *str);

// Example font: 6x8 ASCII 32-127
extern const uint8_t font6x8[];

#endif // SSD1306_H
