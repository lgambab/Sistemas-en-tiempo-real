// sensor_task.c
#include "sensor_task.h"
#include "ntc_driver.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <time.h>
#include <sys/time.h>

static const char *TAG = "sensor_task";

// Datos compartidos protegidos por mutex
static sensor_data_t shared_sensor_data = {0};
static SemaphoreHandle_t sensor_mutex = NULL;
static adc_oneshot_unit_handle_t adc_handle = NULL;

// Tarea que lee sensores periódicamente
static void sensor_update_task(void* arg) {
    ESP_LOGI(TAG, "Sensor task started");
    
    while (1) {
        sensor_data_t local_data = {0};
        
        // Leer temperatura
        if (adc_handle) {
            local_data.temperature_celsius = leer_temperatura_celsius(adc_handle);
            local_data.temperature_valid = (local_data.temperature_celsius > -100);
        } else {
            local_data.temperature_valid = false;
        }
        
        // Leer hora actual
        time_t now;
        time(&now);
        localtime_r(&now, &local_data.time_info);
        local_data.time_valid = (local_data.time_info.tm_year > (2000 - 1900));
        
        // Actualizar datos compartidos de forma segura
        if (xSemaphoreTake(sensor_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            shared_sensor_data = local_data;
            xSemaphoreGive(sensor_mutex);
        }
        
        // Actualizar cada 2 segundos
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

esp_err_t sensor_task_init(adc_oneshot_unit_handle_t handle) {
    adc_handle = handle;
    
    // Crear mutex
    sensor_mutex = xSemaphoreCreateMutex();
    if (!sensor_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }
    
    // Crear tarea de sensores
    BaseType_t ret = xTaskCreate(
        sensor_update_task,
        "sensor_task",
        3072,
        NULL,
        configMAX_PRIORITIES - 3,  // Prioridad media
        NULL
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create sensor task");
        vSemaphoreDelete(sensor_mutex);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Sensor task initialized");
    return ESP_OK;
}

esp_err_t sensor_task_get_data(sensor_data_t* data) {
    if (!data) return ESP_ERR_INVALID_ARG;
    
    // Verificar que el mutex existe antes de usarlo
    if (!sensor_mutex) {
        ESP_LOGW(TAG, "Sensor task not initialized");
        memset(data, 0, sizeof(*data));
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(sensor_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        *data = shared_sensor_data;
        xSemaphoreGive(sensor_mutex);
        return ESP_OK;
    }
    
    ESP_LOGW(TAG, "Failed to acquire mutex");
    return ESP_ERR_TIMEOUT;
}

void sensor_task_update_now(void) {
    // Para implementar: notificar a la tarea para actualización inmediata
    // Por ahora, la tarea actualiza cada 2s automáticamente
}
