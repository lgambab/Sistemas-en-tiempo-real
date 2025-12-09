#ifndef KEYPAD_DRIVER_H
#define KEYPAD_DRIVER_H

#include <stdint.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"

#define KEYPAD_ROWS 4
#define KEYPAD_COLS 4

typedef struct {
    gpio_num_t row_pins[KEYPAD_ROWS];
    gpio_num_t col_pins[KEYPAD_COLS];
    TickType_t debounce_ms; // debounce time in ms (0 = default 100ms)
} keypad_handle_t;

// Initialize keypad GPIOs (configure rows as outputs, cols as inputs with pull-up)
esp_err_t keypad_init(keypad_handle_t* keypad);

// Scan whole keypad and return pressed key or '\0' if none.
// This is a polling scan; it handles debounce internally.
char keypad_get_key(keypad_handle_t* keypad);

// Backwards-compatible: scan triggered by an external column pin (gpio_num_t)
char keypad_scan_from_col(keypad_handle_t* keypad, gpio_num_t col_pin);

#endif // KEYPAD_DRIVER_H