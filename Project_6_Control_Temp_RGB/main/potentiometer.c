// ===== INCLUDES Y CONFIGURACIÓN =====
#include "potentiometer.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "POT";

// ===== CONFIGURACIÓN Y VARIABLES GLOBALES =====
#define DEFAULT_VREF    1100
#define NO_OF_SAMPLES   8

static const adc_channel_t POT_CHANNEL = ADC_CHANNEL_6;

static adc_oneshot_unit_handle_t adc1_handle;
static adc_cali_handle_t adc1_cali_handle = NULL;
static bool do_calibration_init = false;

// ===== FUNCIONES DE CALIBRACIÓN DEL ADC =====
static bool adc_calibration_init(adc_unit_t unit, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "Intentando calibración por curve fitting...");
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
            ESP_LOGI(TAG, "Calibración por curve fitting exitosa");
        }
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "Intentando calibración por line fitting...");
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
            ESP_LOGI(TAG, "Calibración por line fitting exitosa");
        }
    }
#endif
    *out_handle = handle;
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Calibración del ADC exitosa");
    } else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated) {
        ESP_LOGW(TAG, "Calibración no soportada, usando valores sin calibrar");
    } else {
        ESP_LOGE(TAG, "Error en la calibración del ADC");
    }

    return calibrated;
}

// ===== FUNCIONES DE INICIALIZACIÓN =====
void pot_init(void)
{
    ESP_LOGI(TAG, "Inicializando ADC1 para potenciómetro...");
    
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_11,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, POT_CHANNEL, &config));

    do_calibration_init = adc_calibration_init(ADC_UNIT_1, ADC_ATTEN_DB_11, &adc1_cali_handle);

    ESP_LOGI(TAG, "Potenciómetro inicializado en GPIO34 (ADC1_CH6)");
}

// ===== FUNCIONES DE LECTURA =====
static int read_raw_avg(void)
{
    int adc_raw[NO_OF_SAMPLES] = {0};
    int adc_sum = 0;
    
    for (int i = 0; i < NO_OF_SAMPLES; i++) {
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, POT_CHANNEL, &adc_raw[i]));
        adc_sum += adc_raw[i];
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    return adc_sum / NO_OF_SAMPLES;
}

// ===== FUNCIONES PÚBLICAS =====
uint32_t pot_get_voltage_mv(void)
{
    int adc_raw = read_raw_avg();
    int voltage = 0;
    
    if (do_calibration_init) {
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_handle, adc_raw, &voltage));
    } else {
        voltage = (adc_raw * 3300) / 4095;
    }
    
    return (uint32_t)voltage;
}

uint8_t pot_get_percent(void)
{
    uint32_t mv = pot_get_voltage_mv();
    
    const uint32_t MAX_MV = 3300;
    
    if (mv >= MAX_MV) return 100;
    
    uint32_t pct = (mv * 100) / MAX_MV;
    return (uint8_t)pct;
}
