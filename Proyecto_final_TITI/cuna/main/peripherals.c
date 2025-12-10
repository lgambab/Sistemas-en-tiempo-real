#include "peripherals.h"
#include "board_config.h"
#include "keypad/keypad.h"
#include "ssd1306.h"
#include "auth_display.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_err.h"
#include <string.h>

static const char *TAG = "peripherals";

static keypad_handle_t keypad_handle;

// Display update task support
typedef enum {
    DISPLAY_CMD_CLEAR,
    DISPLAY_CMD_SHOW_TEXT,
    DISPLAY_CMD_SHOW_CHAR,
} display_cmd_type_t;

typedef struct {
    display_cmd_type_t type;
    char text[64];
    int x;
    int y;
} display_cmd_t;

static QueueHandle_t display_cmd_queue = NULL;

static void display_task(void* arg) {
    display_cmd_t cmd;
    for (;;) {
        if (xQueueReceive(display_cmd_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            switch (cmd.type) {
                case DISPLAY_CMD_CLEAR:
                    ssd1306_clear();
                    ssd1306_update();
                    break;
                case DISPLAY_CMD_SHOW_TEXT:
                    ssd1306_clear();
                    ssd1306_draw_text(cmd.x, cmd.y, cmd.text);
                    ssd1306_update();
                    break;
                case DISPLAY_CMD_SHOW_CHAR:
                    ssd1306_draw_text(cmd.x, cmd.y, cmd.text);
                    ssd1306_update();
                    break;
            }
        }
    }
}

static void keypad_task(void* arg) {
    ESP_LOGI(TAG, "Keypad task started (polling mode)");
    
    for (;;) {
        char k = keypad_get_key(&keypad_handle);
        
        if (k != '\0') {
            // Solo enviar al sistema de autenticación
            peripherals_send_key_event(k);
        }
        
        // Poll every 50ms
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

esp_err_t peripherals_init(void) {
    esp_err_t err = ESP_OK;

    // Init I2C for OLED
    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = I2C_SCL_GPIO,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ,
    };
    err = i2c_param_config(I2C_PORT_NUM, &i2c_conf);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "i2c_param_config failed: %d", err);
    }
    err = i2c_driver_install(I2C_PORT_NUM, I2C_MODE_MASTER, 0, 0, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "i2c_driver_install failed: %d", err);
    }

    ssd1306_init();
    ssd1306_clear();
    ESP_LOGI(TAG, "SSD1306 initialized");

    // Initialize keypad handle
    ESP_LOGI(TAG, "Initializing keypad...");
    for (int i = 0; i < KEYPAD_ROWS; i++) keypad_handle.row_pins[i] = KEYPAD_ROW_PINS[i];
    for (int i = 0; i < KEYPAD_COLS; i++) keypad_handle.col_pins[i] = KEYPAD_COL_PINS[i];
    keypad_handle.debounce_ms = KEYPAD_DEBOUNCE_MS;  // Ahora en ms, no ticks
    err = keypad_init(&keypad_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "keypad_init failed: %d", err);
        return err;
    }
    ESP_LOGI(TAG, "Keypad GPIO configured");

    // Create display command queue
    display_cmd_queue = xQueueCreate(10, sizeof(display_cmd_t));
    if (!display_cmd_queue) return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "Display queue created");

    // Start display task with high priority
    ESP_LOGI(TAG, "Creating display task...");
    xTaskCreate(display_task, "display_task", 3072, NULL, configMAX_PRIORITIES - 1, NULL);
    
    // Start keypad task (polling mode, no interrupts needed)
    ESP_LOGI(TAG, "Creating keypad task...");
    xTaskCreate(keypad_task, "keypad_task", 4096, NULL, configMAX_PRIORITIES - 2, NULL);

    // Initialize authentication system
    auth_display_init();

    ESP_LOGI(TAG, "Peripherals initialized");
    return ESP_OK;
}

void peripherals_deinit(void) {
    // TODO: remove isr handlers, delete queue, stop task (not implemented)
}

esp_err_t peripherals_oled_show_text(const char* text) {
    if (!text) return ESP_ERR_INVALID_ARG;
    
    display_cmd_t cmd = {
        .type = DISPLAY_CMD_SHOW_TEXT,
        .x = 0,
        .y = 0
    };
    strncpy(cmd.text, text, sizeof(cmd.text) - 1);
    cmd.text[sizeof(cmd.text) - 1] = '\0';
    
    if (xQueueSend(display_cmd_queue, &cmd, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t peripherals_oled_clear(void) {
    display_cmd_t cmd = {
        .type = DISPLAY_CMD_CLEAR,
        .x = 0,
        .y = 0
    };
    if (xQueueSend(display_cmd_queue, &cmd, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t peripherals_oled_draw_text(int x, int y, const char* text) {
    if (!text) return ESP_ERR_INVALID_ARG;
    
    display_cmd_t cmd = {
        .type = DISPLAY_CMD_SHOW_CHAR,
        .x = x,
        .y = y
    };
    strncpy(cmd.text, text, sizeof(cmd.text) - 1);
    cmd.text[sizeof(cmd.text) - 1] = '\0';
    
    if (xQueueSend(display_cmd_queue, &cmd, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

// Simple hook: application may override or register a callback instead
void peripherals_send_key_event(char key) {
    // Default: log. Replace with queue/send to app logic as needed.
    ESP_LOGI(TAG, "Key event forwarded: %c", key);
    
    // Procesar tecla en sistema de autenticación
    auth_display_process_key(key);
}
