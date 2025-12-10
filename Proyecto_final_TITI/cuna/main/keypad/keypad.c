#include "keypad.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "rom/ets_sys.h"

static const char *TAG = "keypad";

static const char keypad_map[KEYPAD_ROWS][KEYPAD_COLS] = {
    {'1','2','3','A'},
    {'4','5','6','B'},
    {'7','8','9','C'},
    {'*','0','#','D'}
};

static TickType_t last_press_tick = 0;

static void small_delay_us(int us) {
    ets_delay_us(us);
}

esp_err_t keypad_init(keypad_handle_t* keypad) {
    if (!keypad) return ESP_ERR_INVALID_ARG;

    if (keypad->debounce_ms == 0)
        keypad->debounce_ms = 80;  // Default debounce

    // --- Configurar FILAS como OUTPUTs ---
    for (int r = 0; r < KEYPAD_ROWS; r++) {
        gpio_reset_pin(keypad->row_pins[r]);
        gpio_set_direction(keypad->row_pins[r], GPIO_MODE_OUTPUT);
        gpio_set_level(keypad->row_pins[r], 0); // Inactivo con pull-down = LOW
        ESP_LOGI(TAG, "ROW %d set OUTPUT LOW (inactive)", r);
    }

    // --- Configurar COLUMNAS como INPUT con PULL-DOWN ---
    for (int c = 0; c < KEYPAD_COLS; c++) {
        gpio_reset_pin(keypad->col_pins[c]);
        gpio_set_direction(keypad->col_pins[c], GPIO_MODE_INPUT);
        gpio_set_pull_mode(keypad->col_pins[c], GPIO_PULLDOWN_ONLY);
        ESP_LOGI(TAG, "COL %d set INPUT PULLDOWN", c);
    }

    last_press_tick = xTaskGetTickCount();

    return ESP_OK;
}

char keypad_get_key(keypad_handle_t* keypad) {
    if (!keypad) return '\0';

    TickType_t now = xTaskGetTickCount();
    if ((now - last_press_tick) < keypad->debounce_ms)
        return '\0'; // Debounce

    for (int row = 0; row < KEYPAD_ROWS; row++) {

        // Activar una fila a la vez (HIGH con pull-down)
        gpio_set_level(keypad->row_pins[row], 1);
        small_delay_us(300);

        // Leer columnas
        for (int col = 0; col < KEYPAD_COLS; col++) {

            if (gpio_get_level(keypad->col_pins[col]) == 1) {

                // Esperar a que la tecla sea soltada
                while (gpio_get_level(keypad->col_pins[col]) == 1) {
                    vTaskDelay(10 / portTICK_PERIOD_MS);
                }

                last_press_tick = xTaskGetTickCount();

                // Restaurar fila
                gpio_set_level(keypad->row_pins[row], 0);

                return keypad_map[row][col];
            }
        }

        // Desactivar fila
        gpio_set_level(keypad->row_pins[row], 0);
    }

    return '\0'; // Sin teclas
}
