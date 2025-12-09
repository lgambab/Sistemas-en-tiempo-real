#include "peripherals.h"
#include "board_config.h"
#include "keypad/keypad.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_err.h"
#include <driver/i2c_master.h>      // ESP-IDF I2C master driver
#include <esp_ssd1306.h>            // SSD1306 component header
#if __has_include("ssd1306.h")
#include "ssd1306.h"
#define HAVE_SSD1306 1
#endif

static const char *TAG = "peripherals";

static QueueHandle_t keypad_evt_queue = NULL; // queue of gpio_num_t events
static keypad_handle_t keypad_handle;

static void IRAM_ATTR gpio_isr_handler(void* arg) {
    gpio_num_t gpio_num = (gpio_num_t)(intptr_t)arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (keypad_evt_queue) xQueueSendFromISR(keypad_evt_queue, &gpio_num, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
}

static void keypad_task(void* arg) {
    gpio_num_t triggered_col;
    for (;;) {
        if (xQueueReceive(keypad_evt_queue, &triggered_col, portMAX_DELAY) == pdTRUE) {
            char k = keypad_scan_from_col(&keypad_handle, triggered_col);
            if (k != '\0') {
                ESP_LOGI(TAG, "Key pressed: %c", k);
                peripherals_send_key_event(k);
#ifdef HAVE_SSD1306
                // simple display: show last key
                char buf[16] = {0};
                buf[0] = k; buf[1] = '\0';
                ssd1306_Fill(Black);
                ssd1306_SetCursor(10, 20);
                ssd1306_WriteString(buf, Font_7x10, White);
                ssd1306_UpdateScreen();
#endif
            }
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

#ifdef HAVE_SSD1306
    ssd1306_Init();
    ssd1306_Fill(Black);
    ssd1306_UpdateScreen();
    ESP_LOGI(TAG, "SSD1306 initialized");
#else
    ESP_LOGW(TAG, "SSD1306 driver not available; OLED functions disabled");
#endif

    // Initialize keypad handle
    for (int i = 0; i < KEYPAD_ROWS; i++) keypad_handle.row_pins[i] = KEYPAD_ROW_PINS[i];
    for (int i = 0; i < KEYPAD_COLS; i++) keypad_handle.col_pins[i] = KEYPAD_COL_PINS[i];
    keypad_handle.debounce_ms = pdMS_TO_TICKS(KEYPAD_DEBOUNCE_MS);
    err = keypad_init(&keypad_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "keypad_init failed: %d", err);
        return err;
    }

    // Create queue and install ISR service
    keypad_evt_queue = xQueueCreate(KEYPAD_EVENT_QUEUE_LEN, sizeof(gpio_num_t));
    if (!keypad_evt_queue) return ESP_ERR_NO_MEM;

    gpio_install_isr_service(0);
    for (int i = 0; i < KEYPAD_COLS; i++) {
        gpio_num_t col = KEYPAD_COL_PINS[i];
        gpio_set_intr_type(col, GPIO_INTR_NEGEDGE);
        gpio_isr_handler_add(col, gpio_isr_handler, (void*)(intptr_t)col);
    }

    // Start keypad task
    xTaskCreate(keypad_task, "keypad_task", 2048, NULL, configMAX_PRIORITIES - 5, NULL);

    ESP_LOGI(TAG, "Peripherals initialized");
    return ESP_OK;
}

void peripherals_deinit(void) {
    // TODO: remove isr handlers, delete queue, stop task (not implemented)
}

esp_err_t peripherals_oled_show_text(const char* text) {
#ifdef HAVE_SSD1306
    if (!text) return ESP_ERR_INVALID_ARG;
    ssd1306_Fill(Black);
    ssd1306_SetCursor(0, 0);
    ssd1306_WriteString(text, Font_7x10, White);
    ssd1306_UpdateScreen();
    return ESP_OK;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

// Simple hook: application may override or register a callback instead
void peripherals_send_key_event(char key) {
    // Default: log. Replace with queue/send to app logic as needed.
    ESP_LOGI(TAG, "Key event forwarded: %c", key);
}
