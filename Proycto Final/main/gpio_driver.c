// gpio_driver.c
#include "gpio_driver.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "GPIO_DRIVER";

#define LED_YELLOW 2
#define LED_BLUE   5

// Semáforo para proteger acceso concurrente a GPIO
static SemaphoreHandle_t gpio_mutex = NULL;

void gpio_init_leds(void) {
    // Crear mutex para proteger acceso a GPIO
    gpio_mutex = xSemaphoreCreateMutex();
    if (gpio_mutex == NULL) {
        ESP_LOGE(TAG, "Error al crear mutex para GPIO");
        return;
    }
    
    gpio_reset_pin(LED_YELLOW);
    gpio_reset_pin(LED_BLUE);
    gpio_set_direction(LED_YELLOW, GPIO_MODE_INPUT_OUTPUT);
    gpio_set_direction(LED_BLUE, GPIO_MODE_INPUT_OUTPUT);
    
    ESP_LOGI(TAG, "GPIO inicializado con protección de mutex");
}

void gpio_set_yellow(bool state) {
    if (gpio_mutex != NULL && xSemaphoreTake(gpio_mutex, portMAX_DELAY) == pdTRUE) {
        gpio_set_level(LED_YELLOW, state);
        xSemaphoreGive(gpio_mutex);
    } else {
        // Fallback si el mutex no está disponible
        gpio_set_level(LED_YELLOW, state);
    }
}

void gpio_set_blue(bool state) {
    if (gpio_mutex != NULL && xSemaphoreTake(gpio_mutex, portMAX_DELAY) == pdTRUE) {
        gpio_set_level(LED_BLUE, state);
        xSemaphoreGive(gpio_mutex);
    } else {
        // Fallback si el mutex no está disponible
        gpio_set_level(LED_BLUE, state);
    }
}

bool gpio_get_yellow(void) {
    bool state = false;
    if (gpio_mutex != NULL && xSemaphoreTake(gpio_mutex, portMAX_DELAY) == pdTRUE) {
        state = gpio_get_level(LED_YELLOW);
        xSemaphoreGive(gpio_mutex);
    } else {
        // Fallback si el mutex no está disponible
        state = gpio_get_level(LED_YELLOW);
    }
    return state;
}

bool gpio_get_blue(void) {
    bool state = false;
    if (gpio_mutex != NULL && xSemaphoreTake(gpio_mutex, portMAX_DELAY) == pdTRUE) {
        state = gpio_get_level(LED_BLUE);
        xSemaphoreGive(gpio_mutex);
    } else {
        // Fallback si el mutex no está disponible
        state = gpio_get_level(LED_BLUE);
    }
    return state;
}