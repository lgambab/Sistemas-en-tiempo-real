#ifndef POT_DRIVER_H
#define POT_DRIVER_H

#include "esp_adc/adc_oneshot.h"

// Inicializa el canal ADC para el Potenciómetro
void pot_init(adc_oneshot_unit_handle_t adc_handle);

// Lee el potenciómetro y devuelve el voltaje en milivoltios
int pot_read_mv(adc_oneshot_unit_handle_t adc_handle);

#endif // POT_DRIVER_H