#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#include "potentiometer.h"
#include "ntc_temp.h"    // NUEVO
#include "rgb_led.h"

static const char *TAG = "MAIN";

// Estructura para mensajes de la cola del potenciómetro
typedef struct {
    uint8_t pot_percent;
    uint32_t pot_voltage_mv;
} pot_data_t;

// Estructura para mensajes de la cola de la temperatura (NUEVO)
typedef struct {
    uint8_t temperature_c; // 0..100 °C
    uint8_t red_percent;   // 0..100 % (mapeado de 10 a 50 °C)
} temp_data_t;

// Colas para comunicación entre tareas
static QueueHandle_t pot_queue = NULL;
static QueueHandle_t temp_queue = NULL; // NUEVA cola

// Tarea para lectura del potenciómetro (SIN CAMBIOS Mayores)
void pot_reading_task(void *arg)
{
    pot_data_t pot_data;
    
    while (1) {
        // Leer datos del potenciómetro
        pot_data.pot_percent = pot_get_percent();
        pot_data.pot_voltage_mv = pot_get_voltage_mv();
        
        ESP_LOGI(TAG, "POT: %d %%   %d mV", pot_data.pot_percent, pot_data.pot_voltage_mv);
        
        // Enviar datos a la cola
        if (xQueueSend(pot_queue, &pot_data, pdMS_TO_TICKS(100)) != pdTRUE) {
            ESP_LOGW(TAG, "Error enviando POT data a la cola");
        }
        
        vTaskDelay(pdMS_TO_TICKS(250)); // leer 4 veces por segundo
    }
}

// NUEVA Tarea para lectura del termistor
void temp_reading_task(void *arg)
{
    temp_data_t temp_data;
    const uint8_t TEMP_MIN = 10; // 10°C es 0%
    const uint8_t TEMP_MAX = 50; // 50°C es 100%
    
    while (1) {
        // 1. Leer temperatura
        temp_data.temperature_c = ntc_get_temperature_celsius();
        
        // 2. Mapear temperatura a porcentaje (0-100) para el LED Rojo
        if (temp_data.temperature_c <= TEMP_MIN) {
            temp_data.red_percent = 0;
        } else if (temp_data.temperature_c >= TEMP_MAX) {
            temp_data.red_percent = 100;
        } else {
            // Mapeo lineal: (T - T_min) / (T_max - T_min) * 100
            temp_data.red_percent = ((temp_data.temperature_c - TEMP_MIN) * 100) / (TEMP_MAX - TEMP_MIN);
        }
        
        ESP_LOGI(TAG, "TEMP: %d °C  -> LED Rojo %d %%", temp_data.temperature_c, temp_data.red_percent);
        
        // 3. Enviar datos a la cola
        if (xQueueSend(temp_queue, &temp_data, pdMS_TO_TICKS(100)) != pdTRUE) {
            ESP_LOGW(TAG, "Error enviando TEMP data a la cola");
        }
        
        vTaskDelay(pdMS_TO_TICKS(500)); // leer 2 veces por segundo (más lento que el POT)
    }
}

// Tarea para control del LED RGB (AHORA maneja ambos colores)
void rgb_control_task(void *arg)
{
    // Variables locales para mantener el estado de ambos colores
    pot_data_t pot_data;
    temp_data_t temp_data;
    
    while (1) {
        BaseType_t pot_rx = xQueueReceive(pot_queue, &pot_data, 0); // Polling sin bloqueo
        BaseType_t temp_rx = xQueueReceive(temp_queue, &temp_data, 0); // Polling sin bloqueo
        
        if (pot_rx == pdTRUE) {
            // Actualizar PWM del LED verde basado en el porcentaje del potenciómetro
            rgb_set_green_percent(pot_data.pot_percent);
            ESP_LOGI(TAG, "LED Verde actualizado: %d%%", pot_data.pot_percent);
        }

        if (temp_rx == pdTRUE) {
            // Actualizar PWM del LED rojo basado en el porcentaje de temperatura
            rgb_set_red_percent(temp_data.red_percent);
            ESP_LOGI(TAG, "LED Rojo actualizado: %d%%", temp_data.red_percent);
        }

        // Si no hay datos, esperamos un poco antes de volver a chequear
        if (pot_rx == pdFALSE && temp_rx == pdFALSE) {
            vTaskDelay(pdMS_TO_TICKS(50)); 
        }
    }
}


void app_main(void)
{
    ESP_LOGI(TAG, "Inicializando componentes...");
    
    // Inicializar hardware
    pot_init();
    ntc_temp_init(); // NUEVO: Inicializar termistor
    rgb_led_init();
    
    // Crear cola para comunicación del Potenciómetro
    pot_queue = xQueueCreate(5, sizeof(pot_data_t));
    if (pot_queue == NULL) {
        ESP_LOGE(TAG, "Error creando la cola del Potenciómetro");
        return;
    }

    // Crear cola para comunicación de la Temperatura (NUEVO)
    temp_queue = xQueueCreate(5, sizeof(temp_data_t));
    if (temp_queue == NULL) {
        ESP_LOGE(TAG, "Error creando la cola de Temperatura");
        return;
    }
    ESP_LOGI(TAG, "Colas creadas exitosamente");
    
    // Crear tarea para lectura del potenciómetro
    if (xTaskCreate(pot_reading_task, "pot_reading_task", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Error creando tarea de lectura del potenciómetro");
        return;
    }

    // Crear tarea para lectura del termistor (NUEVO)
    if (xTaskCreate(temp_reading_task, "temp_reading_task", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Error creando tarea de lectura del termistor");
        return;
    }
    
    // Crear tarea para control del LED RGB (AHORA maneja 2 colas)
    // Le bajamos la prioridad para que las lecturas se completen primero.
    if (xTaskCreate(rgb_control_task, "rgb_control_task", 4096, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Error creando tarea de control del LED");
        return;
    }
    
    ESP_LOGI(TAG, "Todas las tareas creadas exitosamente. Sistema ejecutando...");
}