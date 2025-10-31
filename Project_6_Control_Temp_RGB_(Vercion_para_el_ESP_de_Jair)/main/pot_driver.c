#include "pot_driver.h"

#define POT_ADC_CHANNEL     ADC_CHANNEL_5
#define VOLTAGE_REFERENCE   3300

void pot_init(adc_oneshot_unit_handle_t adc_handle) {
    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, POT_ADC_CHANNEL, &config));
}

int pot_read_mv(adc_oneshot_unit_handle_t adc_handle) {
    int adc_raw;
    adc_oneshot_read(adc_handle, POT_ADC_CHANNEL, &adc_raw);
    // Simplificamos: Asumimos que la calibración no es crítica.
    return (adc_raw * VOLTAGE_REFERENCE) / 4095;
}