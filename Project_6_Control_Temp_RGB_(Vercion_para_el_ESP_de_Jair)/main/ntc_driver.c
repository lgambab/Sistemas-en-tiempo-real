#include "ntc_driver.h"
#include <math.h>

#define NTC_ADC_CHANNEL             ADC_CHANNEL_3
#define VOLTAGE_DIVIDER_RESISTANCE  10000.0f
#define NTC_NOMINAL_RESISTANCE      10000.0f
#define NTC_NOMINAL_TEMPERATURE     25.0f
#define NTC_BETA_COEFFICIENT        3950.0f
#define VOLTAGE_REFERENCE           3300.0f

void ntc_init(adc_oneshot_unit_handle_t adc_handle) {
    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, NTC_ADC_CHANNEL, &config));
}

float ntc_read_celsius(adc_oneshot_unit_handle_t adc_handle) {
    int adc_raw;
    adc_oneshot_read(adc_handle, NTC_ADC_CHANNEL, &adc_raw);

    // Simplificamos: Asumimos que la calibración no es crítica aquí. En un producto real se añadiría.
    float voltage_mv = (adc_raw * VOLTAGE_REFERENCE) / 4095.0f;

    if (voltage_mv >= VOLTAGE_REFERENCE) return -999.0f; // Error

    float ntc_resistance = VOLTAGE_DIVIDER_RESISTANCE * voltage_mv / (VOLTAGE_REFERENCE - voltage_mv);
    
    if (ntc_resistance <= 0) return -999.0f; // Error

    float steinhart = log(ntc_resistance / NTC_NOMINAL_RESISTANCE) / NTC_BETA_COEFFICIENT;
    steinhart += 1.0 / (NTC_NOMINAL_TEMPERATURE + 273.15);
    
    return (1.0 / steinhart) - 273.15;
}