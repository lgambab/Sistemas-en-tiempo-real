// ===== INCLUDES Y CONFIGURACIÓN =====
#include "ntc_sensor.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "hal/adc_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <math.h>

static const char *TAG = "NTC_TEMP_CONTROL";

// ===== VARIABLES GLOBALES =====
static adc_oneshot_unit_handle_t adc1_handle;
static adc_cali_handle_t adc1_cali_handle = NULL;
static ntc_data_t current_ntc_data = {.temperature_c = -999.0, .resistance = 0.0, .raw_adc_value = 0};
static bool data_ready = false;
static SemaphoreHandle_t data_mutex = NULL; // Mutex para proteger acceso concurrente

// ===== FUNCIONES DE CALIBRACIÓN DEL ADC =====
// Crea el manejador de calibración del ADC si el esquema está soportado
static bool adc_calibration_init(adc_unit_t unit, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated) {
        adc_cali_curve_fitting_config_t cali_config = {.unit_id = unit, .atten = atten, .bitwidth = ADC_BITWIDTH_DEFAULT};
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK) calibrated = true;
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated) {
        adc_cali_line_fitting_config_t cali_config = {.unit_id = unit, .atten = atten, .bitwidth = ADC_BITWIDTH_DEFAULT};
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK) calibrated = true;
    }
#endif

    *out_handle = handle;
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Calibración exitosa");
    } else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated) {
        ESP_LOGW(TAG, "Calibración no soportada, use valores sin calibrar");
    } else {
        ESP_LOGE(TAG, "Fallo en la calibración");
    }
    return calibrated;
}

// ===== FUNCIONES DE INICIALIZACIÓN =====
// Inicializa el ADC y el canal del NTC (GPIO32) con atenuación y calibración
// Usamos ADC1 porque ADC2 no funciona cuando WiFi está activo
void ntc_sensor_init(void) {
    ESP_LOGI(TAG, "Inicializando ADC1 para sensor NTC en GPIO32...");
    
    // Crear mutex para proteger acceso concurrente a los datos
    data_mutex = xSemaphoreCreateMutex();
    if (data_mutex == NULL) {
        ESP_LOGE(TAG, "Error al crear mutex para datos del sensor");
        return;
    }
    
    adc_oneshot_unit_init_cfg_t init_config1 = {.unit_id = ADC_UNIT};
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    adc_oneshot_chan_cfg_t config = {.bitwidth = ADC_BITWIDTH_DEFAULT, .atten = ADC_ATTEN_DB_12};
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, NTC_PIN, &config));

    adc_calibration_init(ADC_UNIT, ADC_ATTEN_DB_12, &adc1_cali_handle);
    ESP_LOGI(TAG, "ADC1 inicializado correctamente en GPIO32 (ADC_CHANNEL_4)");
}


// ===== FUNCIONES DE LECTURA Y CÁLCULO =====
// Lee el ADC, calcula la resistencia del NTC y estima la temperatura en °C
ntc_data_t ntc_read_temperature(void) {
    ntc_data_t ntc_data = {0};
    int raw_adc_value;
    
    if (adc_oneshot_read(adc1_handle, NTC_PIN, &raw_adc_value) != ESP_OK) {
        ESP_LOGE(TAG, "Error al leer ADC1");
        ntc_data.temperature_c = -999.0;
        return ntc_data;
    }
    
    ntc_data.raw_adc_value = raw_adc_value;
    
    if (raw_adc_value <= 0 || raw_adc_value >= 4096) {
        ESP_LOGW(TAG, "Valor ADC fuera de rango: %d", raw_adc_value);
        ntc_data.temperature_c = -999.0;
        return ntc_data;
    }
    
    float resistance = SERIES_RESISTOR * ((4095.0 / raw_adc_value) - 1.0);
    ntc_data.resistance = resistance;

    if (resistance <= 0 || resistance >= 1000000) {
        ESP_LOGW(TAG, "Resistencia fuera de rango: %.0fΩ", resistance);
        ntc_data.temperature_c = -999.0;
        return ntc_data;
    }
    
    // Cálculo de temperatura usando ecuación de Steinhart-Hart
    float steinhart = log(resistance / NOMINAL_RESISTANCE) / B_COEFFICIENT + 1.0 / (NOMINAL_TEMPERATURE + 273.15);
    ntc_data.temperature_c = 1.0 / steinhart - 273.15;
    
    return ntc_data;
}

// ===== TAREA DE LECTURA PERIÓDICA =====
// Tarea: Lee temperatura del NTC, valida rangos y almacena datos
static void ntc_reading_task(void *arg)
{
    ntc_data_t ntc_data;
    ESP_LOGI(TAG, "Tarea de lectura del sensor NTC iniciada");
    
    // Esperar un poco para que el ADC se estabilice
    vTaskDelay(pdMS_TO_TICKS(100));
    
    while (1) {
        ntc_data = ntc_read_temperature();
        
        // Proteger acceso concurrente con mutex
        if (xSemaphoreTake(data_mutex, portMAX_DELAY) == pdTRUE) {
            // Siempre almacenar los últimos datos leídos, incluso si no son perfectos
            // Esto permite que el web server vea qué está leyendo el sensor
            current_ntc_data = ntc_data;
            
            // Validar datos para marcar como "ready"
            // Ser más permisivo: aceptar cualquier temperatura calculada que no sea -999.0
            if (ntc_data.raw_adc_value > 0 && ntc_data.raw_adc_value < 4096 && 
                ntc_data.temperature_c != -999.0 && 
                isfinite(ntc_data.temperature_c) && !isnan(ntc_data.temperature_c)) {
                data_ready = true;
            } else {
                data_ready = false;
            }
            
            xSemaphoreGive(data_mutex);
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000)); // Leer cada segundo
    }
}

// ===== FUNCIÓN GETTER =====
// Obtiene la temperatura actual almacenada
ntc_data_t ntc_get_current_temperature(void) {
    ntc_data_t temp_data = {.temperature_c = -999.0, .resistance = 0.0, .raw_adc_value = 0};
    
    // Proteger acceso concurrente con mutex
    if (data_mutex != NULL && xSemaphoreTake(data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        temp_data = current_ntc_data;
        xSemaphoreGive(data_mutex);
    }
    
    return temp_data;
}

// Inicia la tarea de lectura periódica
void ntc_start_reading_task(void) {
    xTaskCreate(ntc_reading_task, "ntc_reader", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "Tarea de lectura periódica del NTC iniciada");
}

