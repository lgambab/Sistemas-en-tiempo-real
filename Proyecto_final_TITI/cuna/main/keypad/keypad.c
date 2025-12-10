#include "keypad.h"
#include "esp_err.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "keypad";

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

    // INVERTIDO: Configure COLUMN pins as outputs, set LOW (inactive with pull-down logic)
    for (int i = 0; i < KEYPAD_COLS; i++) {
        gpio_reset_pin(keypad->col_pins[i]);
        gpio_set_direction(keypad->col_pins[i], GPIO_MODE_OUTPUT);
        gpio_set_drive_capability(keypad->col_pins[i], GPIO_DRIVE_CAP_3);  // Máxima corriente ~40mA
        gpio_set_level(keypad->col_pins[i], 0);
        ESP_LOGI(TAG, "Col %d configured: GPIO %d set to OUTPUT LOW (max drive)", i, keypad->col_pins[i]);
    }

    // INVERTIDO: Configure ROW pins as inputs with pull-down
    for (int i = 0; i < KEYPAD_ROWS; i++) {
        gpio_reset_pin(keypad->row_pins[i]);
        gpio_set_direction(keypad->row_pins[i], GPIO_MODE_INPUT);
        gpio_set_pull_mode(keypad->row_pins[i], GPIO_PULLDOWN_ONLY);
        ESP_LOGI(TAG, "Row %d configured: GPIO %d set to INPUT PULLDOWN", i, keypad->row_pins[i]);
    }

    last_press_tick = xTaskGetTickCount();
    return ESP_OK;
}

// Internal helper: scan given ROW index only (INVERTED: columns drive, rows sense)
static char scan_row_index(keypad_handle_t* keypad, int row_index) {
    char key_pressed = '\0';

    // First, set ALL columns to HIGH to detect which column is pressed
    for (int c = 0; c < KEYPAD_COLS; c++) {
        gpio_set_level(keypad->col_pins[c], 1);
    }
    small_delay_ms(5);
    
    // Check if row is still HIGH (key is still pressed)
    if (gpio_get_level(keypad->row_pins[row_index]) == 0) {
        // Key was released, no detection
        for (int c = 0; c < KEYPAD_COLS; c++) gpio_set_level(keypad->col_pins[c], 0);
        return '\0';
    }

    // Now scan each column individually
    for (int col = 0; col < KEYPAD_COLS; col++) {
        // Drive all columns LOW except current column
        for (int c = 0; c < KEYPAD_COLS; c++) {
            gpio_set_level(keypad->col_pins[c], (c == col) ? 1 : 0);
        }
        
        ESP_LOGI(TAG, "Scanning col %d (GPIO %d = HIGH)", col, keypad->col_pins[col]);

        small_delay_ms(2);

        int level = gpio_get_level(keypad->row_pins[row_index]);
        ESP_LOGI(TAG, "  Row %d (GPIO %d) level: %d", row_index, keypad->row_pins[row_index], level);
        
        if (level == 1) { // active high (pull-down logic)
            key_pressed = keypad_map[row_index][col];

            // Wait for release
            while (gpio_get_level(keypad->row_pins[row_index]) == 1) {
                small_delay_ms(10);
            }

            last_press_tick = xTaskGetTickCount();
            break;
        }
    }

    // restore columns to inactive
    for (int c = 0; c < KEYPAD_COLS; c++) gpio_set_level(keypad->col_pins[c], 0);
    return key_pressed;
}

char keypad_get_key(keypad_handle_t* keypad) {
    if (!keypad) return '\0';

    // debounce: if last press within debounce_ms, skip
    TickType_t now = xTaskGetTickCount();
    if ((now - last_press_tick) < (keypad->debounce_ms ? keypad->debounce_ms : pdMS_TO_TICKS(100))) {
        return '\0';
    }

    // INVERTED: drive columns and check all rows
    for (int row = 0; row < KEYPAD_ROWS; row++) {
        char k = scan_row_index(keypad, row);
        if (k != '\0') return k;
    }

    return '\0';
}

char keypad_scan_from_col(keypad_handle_t* keypad, gpio_num_t col_pin) {
    if (!keypad) return '\0';

    // INVERTED: col_pin is now actually a ROW pin (despite function name)
    int row_index = -1;
    for (int i = 0; i < KEYPAD_ROWS; i++) {
        if (keypad->row_pins[i] == col_pin) {
            row_index = i;
            break;
        }
    }
    if (row_index == -1) return '\0';

    // debounce check
    TickType_t now = xTaskGetTickCount();
    if ((now - last_press_tick) < (keypad->debounce_ms ? keypad->debounce_ms : pdMS_TO_TICKS(100))) {
        return '\0';
    }

    return scan_row_index(keypad, row_index);
}