/**
 * @file sensor_task.c
 * @brief Implementación de tarea RTOS para lectura thread-safe de sensores
 * @author Jair Hernan Telpis Cuaran, Luis Fernando Gamba Bedoya
 * @date 2025
 * 
 * @details
 * Este módulo implementa una arquitectura thread-safe para acceso a sensores en FreeRTOS.
 * Soluciona el problema de compartir el handle del ADC entre múltiples tareas mediante:
 * - Tarea dedicada que lee sensores cada 2 segundos
 * - Mutex para proteger estructura de datos compartida
 * - API de acceso segura para otras tareas (HTTP server, display, etc.)
 * 
 * **Arquitectura RTOS:**
 * @code
 *   [sensor_update_task] ---(cada 2s)---> [Lee NTC + Time]
 *         |                                        |
 *         v                                        v
 *   xSemaphoreTake()                      Actualiza datos
 *         |                                        |
 *         v                                        v
 *   shared_sensor_data <---- Mutex ----> xSemaphoreGive()
 *         ^
 *         |
 *   sensor_task_get_data() <--- Llamado desde otras tareas
 * @endcode
 * 
 * **Ventajas sobre acceso directo:**
 * - No hay race conditions en el ADC handle
 * - Lectura centralizada cada 2s reduce overhead
 * - Todas las tareas obtienen datos consistentes
 * - Cumple con las reglas de RTOS para recursos compartidos
 * 
 * @see sensor_task.h Para la API pública
 * @see ntc_driver.h Para lectura de temperatura
 */

// sensor_task.c
#include "sensor_task.h"
#include "ntc_driver.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <time.h>
#include <sys/time.h>

/** @brief Tag para logging ESP-IDF */
static const char *TAG = "sensor_task";

/**
 * @var shared_sensor_data
 * @brief Datos de sensores compartidos entre tareas
 * 
 * @details
 * Esta variable contiene la última lectura válida de:
 * - Temperatura del NTC (en °C)
 * - Hora actual del sistema (struct tm)
 * - Flags de validez para cada sensor
 * 
 * **IMPORTANTE:** Acceso protegido por sensor_mutex.
 * Nunca acceder directamente - usar sensor_task_get_data().
 */
static sensor_data_t shared_sensor_data = {0};

/**
 * @var sensor_mutex
 * @brief Mutex para protección de shared_sensor_data
 * 
 * @details
 * Implementa exclusión mutua para evitar race conditions.
 * - Toma: sensor_update_task (escritura) y sensor_task_get_data (lectura)
 * - Timeout: 100ms para evitar bloqueos prolongados
 * - Tipo: Mutex binario de FreeRTOS
 */
static SemaphoreHandle_t sensor_mutex = NULL;

/**
 * @var adc_handle
 * @brief Handle del ADC usado para leer el termistor NTC
 * 
 * @details
 * Inicializado por sensor_task_init().
 * Compartido solo dentro de esta tarea (no hay race conditions).
 * Si es NULL, temperature_valid se marca como false.
 */
static adc_oneshot_unit_handle_t adc_handle = NULL;

/**
 * @brief Tarea RTOS que actualiza sensores periódicamente
 * @implements sensor_update_task
 * 
 * @details
 * **Ciclo de ejecución:**
 * 1. Lee temperatura del NTC (si adc_handle válido)
 * 2. Obtiene hora actual del sistema (time() + localtime_r())
 * 3. Valida datos (temperatura > -100°C, año > 2000)
 * 4. Actualiza shared_sensor_data con protección de mutex
 * 5. Espera 2 segundos (vTaskDelay)
 * 
 * **Parámetros de la tarea:**
 * - Nombre: "sensor_task"
 * - Stack: 3072 bytes
 * - Prioridad: configMAX_PRIORITIES - 3 (prioridad media)
 * - Periodo: 2000ms
 * 
 * **Manejo de errores:**
 * - Si ADC handle es NULL: temperature_valid = false
 * - Si temperatura < -100°C: temperatura inválida (error NTC)
 * - Si tm_year < 100: hora aún no sincronizada con SNTP
 * - Si mutex no disponible en 100ms: skip update (sin crash)
 * 
 * @param[in] arg Parámetro de tarea (no usado)
 * 
 * @note Esta tarea nunca termina (bucle infinito)
 * @warning No modificar periodo sin revisar consumo de otros recursos
 */
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

/**
 * @brief Inicializa el subsistema de sensores y crea la tarea RTOS
 * @implements sensor_task_init
 * 
 * @details
 * Secuencia de inicialización:
 * 1. Guarda handle del ADC en variable estática
 * 2. Crea mutex para protección de datos compartidos
 * 3. Crea tarea sensor_update_task con parámetros predefinidos
 * 
 * **Recursos creados:**
 * - 1 Mutex binario (sensor_mutex)
 * - 1 Tarea RTOS (3072 bytes de stack)
 * 
 * @param[in] handle Handle del ADC previamente configurado con ntc_driver
 * 
 * @return esp_err_t
 * @retval ESP_OK Inicialización exitosa
 * @retval ESP_ERR_NO_MEM No hay memoria para crear mutex
 * @retval ESP_FAIL Error al crear tarea RTOS
 * 
 * @warning Llamar solo una vez durante inicialización del sistema
 * @warning El handle del ADC debe estar inicializado con inicializar_ntc()
 * 
 * @see ntc_driver.h Para inicialización del ADC
 */
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

/**
 * @brief Obtiene copia thread-safe de los datos de sensores
 * @implements sensor_task_get_data
 * 
 * @details
 * Implementa lectura segura desde cualquier contexto (tarea o ISR si se usa FromISR).
 * - Adquiere mutex con timeout de 100ms
 * - Copia estructura completa (atomic memcpy)
 * - Libera mutex inmediatamente
 * 
 * **Uso desde múltiples tareas:**
 * Esta función es completamente thread-safe. Puede ser llamada simultáneamente
 * desde HTTP server, display task, keypad task, etc. El mutex garantiza que
 * nunca se lean datos parcialmente actualizados.
 * 
 * **Ejemplo de uso:**
 * @code
 * sensor_data_t data;
 * if (sensor_task_get_data(&data) == ESP_OK) {
 *     if (data.temperature_valid) {
 *         printf("Temp: %.1f C\n", data.temperature_celsius);
 *     }
 *     if (data.time_valid) {
 *         printf("Hora: %02d:%02d\n", data.time_info.tm_hour, data.time_info.tm_min);
 *     }
 * }
 * @endcode
 * 
 * @param[out] data Puntero a estructura donde se copiarán los datos
 * 
 * @return esp_err_t
 * @retval ESP_OK Datos copiados exitosamente
 * @retval ESP_ERR_INVALID_ARG data es NULL
 * @retval ESP_ERR_INVALID_STATE sensor_task_init() no ha sido llamado
 * @retval ESP_ERR_TIMEOUT No se pudo adquirir mutex en 100ms
 * 
 * @note Si retorna error, data se rellena con ceros (memset)
 * @warning No llamar desde ISR - usar versión FromISR si existe
 */
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

/**
 * @brief Solicita actualización inmediata de sensores
 * @implements sensor_task_update_now
 * 
 * @details
 * **Nota de implementación:**
 * Actualmente no implementado (stub). La tarea actualiza automáticamente cada 2s.
 * 
 * **Implementación futura:**
 * Podría usar xTaskNotify() para despertar sensor_update_task inmediatamente:
 * @code
 * static TaskHandle_t sensor_task_handle = NULL;
 * 
 * // En sensor_task_init:
 * xTaskCreate(..., &sensor_task_handle);
 * 
 * // En sensor_update_task:
 * ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2000)); // Wait 2s o notificación
 * 
 * // En sensor_task_update_now:
 * if (sensor_task_handle) {
 *     xTaskNotifyGive(sensor_task_handle);
 * }
 * @endcode
 * 
 * @note Por ahora, esperar hasta 2 segundos para nueva lectura automática
 */
void sensor_task_update_now(void) {
    // Para implementar: notificar a la tarea para actualización inmediata
    // Por ahora, la tarea actualiza cada 2s automáticamente
}
