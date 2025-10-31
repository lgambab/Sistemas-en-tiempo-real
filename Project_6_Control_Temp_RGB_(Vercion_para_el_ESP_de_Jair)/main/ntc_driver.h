#ifndef NTC_DRIVER_H
#define NTC_DRIVER_H

#include "esp_adc/adc_oneshot.h"

// Inicializa el canal ADC para el NTC
void ntc_init(adc_oneshot_unit_handle_t adc_handle);

// Lee el NTC y devuelve la temperatura en grados Celsius
float ntc_read_celsius(adc_oneshot_unit_handle_t adc_handle);

#endif // NTC_DRIVER_H