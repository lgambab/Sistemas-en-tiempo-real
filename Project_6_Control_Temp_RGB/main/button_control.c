// ===== INCLUDES Y CONFIGURACIÓN =====
#include "button_control.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"

static const char *TAG = "BUTTON_CONTROL";

// ===== VARIABLES GLOBALES =====
static button_state_t button_state = {0};
QueueHandle_t button_queue = NULL;

// ===== FUNCIONES DE INICIALIZACIÓN =====
void button_control_init(void) {
    ESP_LOGI(TAG, "Inicializando control de botón en GPIO%d...", BUTTON_PIN);
    
    // Configuración del GPIO del botón
    gpio_config_t button_config = {
        .pin_bit_mask = (1ULL << BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&button_config));
    
    // Inicializar estado del botón
    button_state.is_pressed = false;
    button_state.print_enabled = true;  // Por defecto, la impresión está habilitada
    button_state.press_start_time = 0;
    button_state.last_event_time = 0;
    
    // Crear cola de eventos del botón
    button_queue = xQueueCreate(5, sizeof(button_event_t));
    if (button_queue == NULL) {
        ESP_LOGE(TAG, "Error creando cola de eventos del botón");
        return;
    }
    
    ESP_LOGI(TAG, "Control de botón inicializado correctamente");
    ESP_LOGI(TAG, "Estado inicial: Impresión %s", button_state.print_enabled ? "HABILITADA" : "DESHABILITADA");
}

// ===== FUNCIONES DE LECTURA DEL BOTÓN =====
static bool read_button_state(void) {
    return !gpio_get_level(BUTTON_PIN);  // Invertido porque usamos pull-up
}

static uint32_t get_current_time_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// ===== FUNCIONES DE DEBOUNCE Y DETECCIÓN =====
static bool is_debounce_ready(void) {
    uint32_t current_time = get_current_time_ms();
    return (current_time - button_state.last_event_time) >= BUTTON_DEBOUNCE_TIME;
}

static bool is_long_press(void) {
    if (!button_state.is_pressed) return false;
    uint32_t current_time = get_current_time_ms();
    return (current_time - button_state.press_start_time) >= BUTTON_LONG_PRESS_TIME;
}

// ===== FUNCIONES DE CONTROL =====
void button_task(void *arg) {
    ESP_LOGI(TAG, "Tarea de control de botón iniciada");
    
    bool last_button_state = false;
    bool long_press_detected = false;
    
    while (1) {
        bool current_button_state = read_button_state();
        uint32_t current_time = get_current_time_ms();
        
        // Detectar flanco de bajada (botón presionado)
        if (current_button_state && !last_button_state && is_debounce_ready()) {
            button_state.is_pressed = true;
            button_state.press_start_time = current_time;
            button_state.last_event_time = current_time;
            long_press_detected = false;
            
            ESP_LOGI(TAG, "Botón presionado");
        }
        
        // Detectar flanco de subida (botón liberado)
        else if (!current_button_state && last_button_state && is_debounce_ready()) {
            button_state.is_pressed = false;
            button_state.last_event_time = current_time;
            
            if (!long_press_detected) {
                // Pulsación corta: alternar impresión
                button_state.print_enabled = !button_state.print_enabled;
                ESP_LOGI(TAG, "Pulsación corta - Impresión %s", 
                         button_state.print_enabled ? "HABILITADA" : "DESHABILITADA");
                
                // Enviar evento a la cola
                button_event_t event = BUTTON_EVENT_PRESSED;
                xQueueSend(button_queue, &event, pdMS_TO_TICKS(10));
            }
            
            ESP_LOGI(TAG, "Botón liberado");
        }
        
        // Detectar pulsación larga
        else if (button_state.is_pressed && is_long_press() && !long_press_detected) {
            long_press_detected = true;
            ESP_LOGI(TAG, "Pulsación larga detectada");
            
            // Enviar evento de pulsación larga
            button_event_t event = BUTTON_EVENT_LONG_PRESS;
            xQueueSend(button_queue, &event, pdMS_TO_TICKS(10));
        }
        
        last_button_state = current_button_state;
        vTaskDelay(pdMS_TO_TICKS(10));  // Polling cada 10ms
    }
}

// ===== FUNCIONES PÚBLICAS =====
bool is_print_enabled(void) {
    return button_state.print_enabled;
}

void set_print_enabled(bool enabled) {
    button_state.print_enabled = enabled;
    ESP_LOGI(TAG, "Impresión %s manualmente", enabled ? "HABILITADA" : "DESHABILITADA");
}

button_state_t get_button_state(void) {
    return button_state;
}
