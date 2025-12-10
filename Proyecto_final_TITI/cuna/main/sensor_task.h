// sensor_task.h
// Tarea dedicada para lectura de sensores (temperatura, hora) con acceso seguro
#ifndef SENSOR_TASK_H
#define SENSOR_TASK_H

#include "esp_err.h"
#include "esp_adc/adc_oneshot.h"
#include <time.h>

// Estructura con datos de sensores (protegida por mutex)
typedef struct {
    float temperature_celsius;
    struct tm time_info;
    bool time_valid;
    bool temperature_valid;
} sensor_data_t;

// Inicializar tarea de sensores
esp_err_t sensor_task_init(adc_oneshot_unit_handle_t adc_handle);

// Obtener datos de sensores (thread-safe)
esp_err_t sensor_task_get_data(sensor_data_t* data);

// Forzar actualización inmediata de sensores
void sensor_task_update_now(void);

#endif // SENSOR_TASK_H
