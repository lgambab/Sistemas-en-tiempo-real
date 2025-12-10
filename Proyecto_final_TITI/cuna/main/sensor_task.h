/**
 * @file sensor_task.h
 * @brief Tarea dedicada para lectura de sensores con acceso thread-safe mediante mutex
 * @author Jair Hernan Telpis Cuaran, Luis Fernando Gamba Bedoya
 * @date 2025
 * @version 1.0.0
 * 
 * @details Esta tarea RTOS lee periódicamente los sensores (temperatura NTC y hora del sistema)
 * y almacena los datos en una estructura protegida por mutex, garantizando acceso concurrente
 * seguro desde múltiples tareas (HTTP server, pantalla OLED, etc).
 * 
 * Universidad Nacional de Colombia - Curso RTOS
 */

#ifndef SENSOR_TASK_H
#define SENSOR_TASK_H

#include "esp_err.h"
#include "esp_adc/adc_oneshot.h"
#include <time.h>

/**
 * @struct sensor_data_t
 * @brief Estructura con datos de sensores protegida por mutex
 * 
 * Contiene las últimas lecturas válidas de temperatura, hora del sistema
 * y flags de validez para cada dato.
 */
typedef struct {
    float temperature_celsius;  /**< Temperatura en grados Celsius del sensor NTC */
    struct tm time_info;        /**< Información de tiempo del sistema (SNTP) */
    bool time_valid;            /**< true si la hora está sincronizada por SNTP */
    bool temperature_valid;     /**< true si la lectura de temperatura es válida */
} sensor_data_t;

/**
 * @brief Inicializar tarea de sensores
 * 
 * Crea una tarea FreeRTOS que lee sensores cada 2 segundos y actualiza
 * los datos compartidos de forma thread-safe usando un mutex.
 * 
 * @param adc_handle Handle del ADC configurado para el sensor NTC
 * @return ESP_OK si la inicialización fue exitosa
 * @return ESP_ERR_NO_MEM si no hay memoria para crear el mutex o la tarea
 * @return ESP_FAIL si falla la creación de la tarea
 * 
 * @note Esta función debe llamarse después de inicializar el ADC y antes
 *       de cualquier llamada a sensor_task_get_data()
 */
esp_err_t sensor_task_init(adc_oneshot_unit_handle_t adc_handle);

/**
 * @brief Obtener datos de sensores de forma thread-safe
 * 
 * Lee los datos de sensores desde la estructura compartida protegida
 * por mutex. Bloquea hasta 100ms esperando el mutex.
 * 
 * @param[out] data Puntero a estructura donde se copiarán los datos
 * @return ESP_OK si los datos fueron leídos correctamente
 * @return ESP_ERR_INVALID_ARG si data es NULL
 * @return ESP_ERR_TIMEOUT si no se pudo adquirir el mutex en 100ms
 * 
 * @warning Siempre verificar el return value antes de usar los datos
 * @note Esta función es thread-safe y puede llamarse desde cualquier tarea
 */
esp_err_t sensor_task_get_data(sensor_data_t* data);

/**
 * @brief Forzar actualización inmediata de sensores
 * 
 * Notifica a la tarea de sensores para que realice una lectura inmediata
 * en lugar de esperar el próximo intervalo de 2 segundos.
 * 
 * @note Implementación futura - actualmente la tarea actualiza cada 2s automáticamente
 */
void sensor_task_update_now(void);

#endif // SENSOR_TASK_H
