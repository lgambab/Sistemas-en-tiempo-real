#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#include "potentiometer.h"
#include "rgb_led.h"

static const char *TAG = "MAIN";

// Estructura para mensajes de la cola
typedef struct {
    uint8_t pot_percent;
    uint32_t pot_voltage_mv;
} pot_data_t;

// Cola para comunicación entre tareas
static QueueHandle_t pot_queue = NULL;

// Tarea para lectura del potenciómetro
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
            ESP_LOGW(TAG, "Error enviando datos a la cola");
        }
        
        vTaskDelay(pdMS_TO_TICKS(250)); // leer 4 veces por segundo
    }
}

// Tarea para control del LED RGB
void rgb_control_task(void *arg)
{
    pot_data_t received_data;
    
    while (1) {
        // Recibir datos de la cola
        if (xQueueReceive(pot_queue, &received_data, portMAX_DELAY) == pdTRUE) {
            // Actualizar PWM del LED verde basado en el porcentaje del potenciómetro
            rgb_set_green_percent(received_data.pot_percent);
            ESP_LOGI(TAG, "LED actualizado: %d%%", received_data.pot_percent);
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Inicializando componentes...");
    
    // Inicializar hardware
    pot_init();
    rgb_led_init();
    
    // Crear cola para comunicación entre tareas
    pot_queue = xQueueCreate(5, sizeof(pot_data_t));
    if (pot_queue == NULL) {
        ESP_LOGE(TAG, "Error creando la cola");
        return;
    }
    ESP_LOGI(TAG, "Cola creada exitosamente");
    
    // Crear tarea para lectura del potenciómetro
    if (xTaskCreate(pot_reading_task, "pot_reading_task", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Error creando tarea de lectura del potenciómetro");
        return;
    }
    
    // Crear tarea para control del LED RGB
    if (xTaskCreate(rgb_control_task, "rgb_control_task", 4096, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Error creando tarea de control del LED");
        return;
    }
    
    ESP_LOGI(TAG, "Todas las tareas creadas exitosamente. Sistema ejecutando...");
}
