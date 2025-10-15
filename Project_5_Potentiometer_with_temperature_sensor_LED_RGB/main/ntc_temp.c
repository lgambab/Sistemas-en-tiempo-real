#include "ntc_temp.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static const char *TAG = "NTC_TEMP";

// Parámetros del NTC (Ajustar según el componente real)
#define NTC_R_NOMINAL   10000.0f  // Resistencia nominal a 25°C (Ohms) -> Usando 10kOhm
#define NTC_T_NOMINAL   298.15f   // Temperatura nominal en Kelvin (25°C)
#define NTC_B_VALUE     3950.0f   // Coeficiente B (Kelvin)
#define NTC_R_REF       10000.0f  // Resistencia de la resistencia de referencia (Ohms)
#define ADC_MAX_VOLTAGE 3300.0f   // Máximo voltaje del ADC en mV

// ADC pin: GPIO35 -> ADC1_CH7
static const adc_channel_t NTC_CHANNEL = ADC_CHANNEL_7;
extern adc_oneshot_unit_handle_t adc1_handle; // Compartido con potentiometer.c
static adc_cali_handle_t adc1_cali_handle = NULL;
static bool do_calibration_init = false;

// Funciones de calibración (tomadas de potentiometer.c)
static bool adc_calibration_init(adc_unit_t unit, adc_atten_t atten, adc_cali_handle_t *out_handle);
static int read_raw_avg(void);
static uint32_t get_voltage_mv(void);

// ... (Incluir funciones de adc_calibration_init, read_raw_avg y get_voltage_mv de potentiometer.c aquí,
//      pero usando NTC_CHANNEL, ADC_UNIT_1 y adc1_handle) ...
// Nota: Por brevedad, asumo que estas funciones se definen o se adaptan. Para el ejercicio, 
// usaré una versión simplificada de la lectura y calibración.

// *** Adaptación simplificada de las funciones de lectura/calibración de 'potentiometer.c' ***

static bool adc_calibration_init(adc_unit_t unit, adc_atten_t atten, adc_cali_handle_t *out_handle) {
    // Tomado de potentiometer.c: Solo para inicializar la calibración
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;
    // ... (Lógica de curve_fitting y line_fitting) ...
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED || ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    // Simplified logic: Assume calibration is attempted
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = unit, .atten = atten, .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
    if (ret == ESP_OK) calibrated = true;
#endif
    *out_handle = handle;
    if (ret == ESP_OK) { ESP_LOGI(TAG, "ADC calibration success"); }
    else { ESP_LOGW(TAG, "ADC calibration skipped/failed"); }
    return calibrated;
}

static int read_raw_avg(void)
{
    int adc_raw = 0;
    // Lectura simple (sin promedio como en potentiometer.c)
    ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, NTC_CHANNEL, &adc_raw));
    return adc_raw;
}

static uint32_t get_voltage_mv(void)
{
    int adc_raw = read_raw_avg();
    int voltage = 0;
    
    if (do_calibration_init) {
        // Usa la calibración si está disponible
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_handle, adc_raw, &voltage));
    } else {
        // Fallback: Conversión simple (12-bit ADC por defecto en ESP32)
        voltage = (adc_raw * 3300) / 4095; 
    }
    
    return (uint32_t)voltage;
}

// *** Fin de la Adaptación ***

void ntc_temp_init(void)
{
    // ADC1 ya está inicializado por potentiometer.c
    // Solo necesitamos configurar nuestro canal
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, NTC_CHANNEL, &config));

    //-------------ADC1 Calibration Init---------------//
    do_calibration_init = adc_calibration_init(ADC_UNIT_1, ADC_ATTEN_DB_12, &adc1_cali_handle);

    ESP_LOGI(TAG, "NTC Thermistor inicializado (ADC1 channel %d)", NTC_CHANNEL);
}

uint8_t ntc_get_temperature_celsius(void)
{
    // 1. Obtener voltaje en mV y mostrar para debug
    uint32_t voltage_mv = get_voltage_mv();
    ESP_LOGI(TAG, "NTC Voltaje leído: %d mV", voltage_mv);
    
    // 2. Calcular la resistencia del NTC usando el divisor de voltaje
    // En tu caso: NTC está a GND, por lo que la fórmula es diferente
    // R_NTC = R_REF * (Vout) / (Vin - Vout)
    float ntc_resistance;
    if (voltage_mv >= ADC_MAX_VOLTAGE) {
        ntc_resistance = 100.0f; // Resistencia muy baja (temperatura alta)
    } else if (voltage_mv == 0) {
        ntc_resistance = 1000000.0f; // Resistencia muy alta (temperatura baja)
    } else {
        ntc_resistance = NTC_R_REF * ((float)voltage_mv / (ADC_MAX_VOLTAGE - voltage_mv));
    }
    
    ESP_LOGI(TAG, "NTC Resistencia calculada: %.2f ohms", ntc_resistance);
    
    // 3. Usar la ecuación de B-parameter para obtener la temperatura
    // 1/T = 1/T0 + (1/B) * ln(R/R0)
    float temp_kelvin = 1.0f / (
        (1.0f / NTC_T_NOMINAL) + 
        (1.0f / NTC_B_VALUE) * logf(ntc_resistance / NTC_R_NOMINAL)
    );
    
    // 4. Convertir a Celsius y limitar a uint8_t (0-255)
    float temp_celsius = temp_kelvin - 273.15f;
    
    ESP_LOGI(TAG, "NTC Temperatura calculada: %.2f °C", temp_celsius);
    
    // Limitar el rango
    if (temp_celsius < 0) return 0;
    if (temp_celsius > 100) return 100;
    
    return (uint8_t)temp_celsius;
}

