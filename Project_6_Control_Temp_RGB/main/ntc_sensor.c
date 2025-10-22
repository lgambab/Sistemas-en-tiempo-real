// ===== INCLUDES Y CONFIGURACIÓN =====
#include "ntc_sensor.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static const char *TAG = "NTC_TEMP_CONTROL";

// ===== VARIABLES GLOBALES =====
static adc_oneshot_unit_handle_t adc2_handle;
static adc_cali_handle_t adc2_cali_handle = NULL;

// ===== FUNCIONES DE CALIBRACIÓN DEL ADC =====
static bool adc_calibration_init(adc_unit_t unit, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "Calibración por curve fitting");
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "Calibración por line fitting");
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
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
void ntc_sensor_init(void) {
    ESP_LOGI(TAG, "Inicializando ADC2 para sensor NTC...");
    
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc2_handle));

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc2_handle, NTC_PIN, &config));

    adc_calibration_init(ADC_UNIT, ADC_ATTEN_DB_12, &adc2_cali_handle);
    
    ESP_LOGI(TAG, "ADC2 inicializado correctamente en GPIO26 (ADC_CHANNEL_9)");
}


// ===== FUNCIONES DE LECTURA Y CÁLCULO =====
ntc_data_t ntc_read_temperature(void) {
    ntc_data_t ntc_data = {0};
    int raw_adc_value;
    
    esp_err_t result = adc_oneshot_read(adc2_handle, NTC_PIN, &raw_adc_value);

    if (result == ESP_OK) {
        ntc_data.raw_adc_value = raw_adc_value;
        
        // Verificar que el valor ADC sea razonable
        if (raw_adc_value > 0 && raw_adc_value < 4096) {
            float resistance = SERIES_RESISTOR * ((4095.0 / raw_adc_value) - 1.0);
            ntc_data.resistance = resistance;

            // Verificar que la resistencia sea razonable
            if (resistance > 0 && resistance < 1000000) { // Entre 0 y 1MΩ
                float steinhart;
                steinhart = resistance / NOMINAL_RESISTANCE;
                steinhart = log(steinhart);
                steinhart /= B_COEFFICIENT;
                steinhart += 1.0 / (NOMINAL_TEMPERATURE + 273.15);
                steinhart = 1.0 / steinhart;
                float temperature_c = steinhart - 273.15;
                ntc_data.temperature_c = temperature_c;
                
                ESP_LOGD(TAG, "Lectura exitosa: ADC=%d, R=%.0fΩ, T=%.1f°C", 
                         raw_adc_value, resistance, temperature_c);
            } else {
                ESP_LOGW(TAG, "Resistencia fuera de rango: %.0fΩ", resistance);
                ntc_data.temperature_c = -999.0; // Valor de error
            }
        } else {
            ESP_LOGW(TAG, "Valor ADC fuera de rango: %d", raw_adc_value);
            ntc_data.temperature_c = -999.0; // Valor de error
        }
    } else {
        ESP_LOGE(TAG, "Error al leer ADC2: %s", esp_err_to_name(result));
        ntc_data.temperature_c = -999.0; // Valor de error
    }

    return ntc_data;
}

