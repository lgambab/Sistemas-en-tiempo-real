#include "keypad.h"
#include "esp_err.h"
#include "freertos/task.h"
#include "esp_timer.h"

static const char keypad_map[KEYPAD_ROWS][KEYPAD_COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

static TickType_t last_press_tick = 0;

static void small_delay_ms(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

esp_err_t keypad_init(keypad_handle_t* keypad) {
    if (!keypad) return ESP_ERR_INVALID_ARG;
    if (keypad->debounce_ms == 0) keypad->debounce_ms = pdMS_TO_TICKS(100);

    // Configure row pins as outputs, set HIGH (inactive)
    for (int i = 0; i < KEYPAD_ROWS; i++) {
        gpio_reset_pin(keypad->row_pins[i]);
        gpio_set_direction(keypad->row_pins[i], GPIO_MODE_OUTPUT);
        gpio_set_level(keypad->row_pins[i], 1);
    }

    // Configure column pins as inputs with pull-up
    for (int i = 0; i < KEYPAD_COLS; i++) {
        gpio_reset_pin(keypad->col_pins[i]);
        gpio_set_direction(keypad->col_pins[i], GPIO_MODE_INPUT);
        gpio_set_pull_mode(keypad->col_pins[i], GPIO_PULLUP_ONLY);
    }

    last_press_tick = xTaskGetTickCount();
    return ESP_OK;
}

// Internal helper: scan given column index only
static char scan_column_index(keypad_handle_t* keypad, int col_index) {
    char key_pressed = '\0';

    for (int row = 0; row < KEYPAD_ROWS; row++) {
        // Drive all rows HIGH then pull current row LOW
        for (int r = 0; r < KEYPAD_ROWS; r++) gpio_set_level(keypad->row_pins[r], 1);
        gpio_set_level(keypad->row_pins[row], 0);

        small_delay_ms(5);

        int level = gpio_get_level(keypad->col_pins[col_index]);
        if (level == 0) { // active low
            key_pressed = keypad_map[row][col_index];

            // Wait for release
            while (gpio_get_level(keypad->col_pins[col_index]) == 0) {
                small_delay_ms(10);
            }

            last_press_tick = xTaskGetTickCount();
            break;
        }
    }

    // restore rows to inactive
    for (int r = 0; r < KEYPAD_ROWS; r++) gpio_set_level(keypad->row_pins[r], 1);
    return key_pressed;
}

char keypad_get_key(keypad_handle_t* keypad) {
    if (!keypad) return '\0';

    // debounce: if last press within debounce_ms, skip
    TickType_t now = xTaskGetTickCount();
    if ((now - last_press_tick) < (keypad->debounce_ms ? keypad->debounce_ms : pdMS_TO_TICKS(100))) {
        return '\0';
    }

    // drive rows and check all columns
    for (int col = 0; col < KEYPAD_COLS; col++) {
        char k = scan_column_index(keypad, col);
        if (k != '\0') return k;
    }

    return '\0';
}

char keypad_scan_from_col(keypad_handle_t* keypad, gpio_num_t col_pin) {
    if (!keypad) return '\0';

    int col_index = -1;
    for (int i = 0; i < KEYPAD_COLS; i++) {
        if (keypad->col_pins[i] == col_pin) {
            col_index = i;
            break;
        }
    }
    if (col_index == -1) return '\0';

    // debounce check
    TickType_t now = xTaskGetTickCount();
    if ((now - last_press_tick) < (keypad->debounce_ms ? keypad->debounce_ms : pdMS_TO_TICKS(100))) {
        return '\0';
    }

    return scan_column_index(keypad, col_index);
}