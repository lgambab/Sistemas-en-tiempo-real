// board_config.h
// Centralized hardware pin definitions for ESP32 board.
#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include "driver/gpio.h"

// Keypad matrix pins - adjust these to match your wiring
// Rows are driven as outputs, Columns are inputs with pull-ups
static const gpio_num_t KEYPAD_ROW_PINS[4] = { GPIO_NUM_12, GPIO_NUM_13, GPIO_NUM_14, GPIO_NUM_27 };
// Changed from GPIO 34, 35 (input-only) to GPIO 25, 26 (support internal pull-ups)
static const gpio_num_t KEYPAD_COL_PINS[4] = { GPIO_NUM_32, GPIO_NUM_33, GPIO_NUM_25, GPIO_NUM_26 };

// Debounce in ms used by keypad (0 = default 100ms)
#define KEYPAD_DEBOUNCE_MS 100

// I2C pins for OLED (SSD1306)
#define I2C_PORT_NUM I2C_NUM_0
#define I2C_SDA_GPIO GPIO_NUM_4
#define I2C_SCL_GPIO GPIO_NUM_15
#define I2C_FREQ_HZ 400000

// Queue length for keypad events
#define KEYPAD_EVENT_QUEUE_LEN 8

#endif // BOARD_CONFIG_H
