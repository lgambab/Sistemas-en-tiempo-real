#include "peripherals.h"
#include "board_config.h"
#include "keypad/keypad.h"
#include "ssd1306.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_err.h"
#include <string.h>

static const char *TAG = "peripherals";

static QueueHandle_t keypad_evt_queue = NULL; // queue of gpio_num_t events
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

static void IRAM_ATTR gpio_isr_handler(void* arg) {
    gpio_num_t gpio_num = (gpio_num_t)(intptr_t)arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (keypad_evt_queue) {
        xQueueSendFromISR(keypad_evt_queue, &gpio_num, &xHigherPriorityTaskWoken);
    }
    if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
}

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
                    ssd1306_clear();
                    ssd1306_draw_text(cmd.x, cmd.y, cmd.text);
                    ssd1306_update();
                    break;
            }
        }
    }
}

static void keypad_task(void* arg) {
    gpio_num_t triggered_col;
    static TickType_t last_key_time = 0;
    const TickType_t debounce_time = pdMS_TO_TICKS(250); // 250ms debounce
    
    ESP_LOGI(TAG, "Keypad task started");
    
    for (;;) {
        if (xQueueReceive(keypad_evt_queue, &triggered_col, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "Interrupt received on GPIO %d", triggered_col);
            
            // Check debounce time
            TickType_t now = xTaskGetTickCount();
            if ((now - last_key_time) < debounce_time) {
                ESP_LOGI(TAG, "Debounce: ignoring (too soon)");
                continue;
            }
            
            // Disable interrupts on all columns
            for (int i = 0; i < KEYPAD_COLS; i++) {
                gpio_intr_disable(KEYPAD_COL_PINS[i]);
            }
            
            // Small delay to let signal stabilize
            vTaskDelay(pdMS_TO_TICKS(20));
            
            ESP_LOGI(TAG, "Scanning keypad from col GPIO %d", triggered_col);
            char k = keypad_scan_from_col(&keypad_handle, triggered_col);
            
            if (k != '\0') {
                // Update last_key_time for debounce
                last_key_time = xTaskGetTickCount();
                ESP_LOGI(TAG, "Key pressed: %c", k);
                peripherals_send_key_event(k);
                
                // Send display command to display task
                display_cmd_t cmd = {
                    .type = DISPLAY_CMD_SHOW_CHAR,
                    .x = 40,
                    .y = 28
                };
                cmd.text[0] = k;
                cmd.text[1] = '\0';
                xQueueSend(display_cmd_queue, &cmd, pdMS_TO_TICKS(100));
            } else {
                ESP_LOGI(TAG, "No key detected after scan");
            }
            
            // Clear the interrupt status for all column pins
            for (int i = 0; i < KEYPAD_COLS; i++) {
                gpio_intr_disable(KEYPAD_COL_PINS[i]);
            }
            
            // Drain the queue of any accumulated events
            gpio_num_t dummy;
            while (xQueueReceive(keypad_evt_queue, &dummy, 0) == pdTRUE) {
                ESP_LOGI(TAG, "Drained extra event from queue: GPIO %d", dummy);
            }
            
            // Wait for mechanical bounce to settle
            vTaskDelay(pdMS_TO_TICKS(100));
            
            // Re-enable interrupts
            for (int i = 0; i < KEYPAD_COLS; i++) {
                gpio_intr_enable(KEYPAD_COL_PINS[i]);
            }
            ESP_LOGI(TAG, "Ready for next keypress");
        }
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
    keypad_handle.debounce_ms = pdMS_TO_TICKS(KEYPAD_DEBOUNCE_MS);
    err = keypad_init(&keypad_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "keypad_init failed: %d", err);
        return err;
    }
    ESP_LOGI(TAG, "Keypad GPIO configured");

    // Create queue and install ISR service
    keypad_evt_queue = xQueueCreate(KEYPAD_EVENT_QUEUE_LEN, sizeof(gpio_num_t));
    if (!keypad_evt_queue) return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "Keypad queue created");

    // Create display command queue
    display_cmd_queue = xQueueCreate(10, sizeof(display_cmd_t));
    if (!display_cmd_queue) return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "Display queue created");

    ESP_LOGI(TAG, "Installing GPIO ISR service...");
    gpio_install_isr_service(0);
    
    ESP_LOGI(TAG, "Configuring column interrupts...");
    for (int i = 0; i < KEYPAD_COLS; i++) {
        gpio_num_t col = KEYPAD_COL_PINS[i];
        ESP_LOGI(TAG, "Setting up interrupt for col %d (GPIO %d)", i, col);
        gpio_set_intr_type(col, GPIO_INTR_NEGEDGE);
        err = gpio_isr_handler_add(col, gpio_isr_handler, (void*)(intptr_t)col);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add ISR handler for GPIO %d: %d", col, err);
        }
    }
    ESP_LOGI(TAG, "All interrupts configured");

    // Start display task with high priority
    ESP_LOGI(TAG, "Creating display task...");
    xTaskCreate(display_task, "display_task", 3072, NULL, configMAX_PRIORITIES - 1, NULL);
    
    // Start keypad task with high priority
    ESP_LOGI(TAG, "Creating keypad task...");
    xTaskCreate(keypad_task, "keypad_task", 2048, NULL, configMAX_PRIORITIES - 2, NULL);

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

// Simple hook: application may override or register a callback instead
void peripherals_send_key_event(char key) {
    // Default: log. Replace with queue/send to app logic as needed.
    ESP_LOGI(TAG, "Key event forwarded: %c", key);
}
