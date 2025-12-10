#ifndef KEYPAD_H
#define KEYPAD_H

#include "driver/gpio.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#define KEYPAD_ROWS 4
#define KEYPAD_COLS 4

typedef struct {
    gpio_num_t row_pins[KEYPAD_ROWS];
    gpio_num_t col_pins[KEYPAD_COLS];
    TickType_t debounce_ms;
} keypad_handle_t;

esp_err_t keypad_init(keypad_handle_t* keypad);
char keypad_get_key(keypad_handle_t* keypad);

#endif