#ifndef CONTROLADOR_NTC_H
#define CONTROLADOR_NTC_H

#include "esp_adc/adc_oneshot.h" // Librería para usar el conversor Analógico-Digital (ADC).


void inicializar_ntc(adc_oneshot_unit_handle_t manejador_adc);


float leer_temperatura_celsius(adc_oneshot_unit_handle_t manejador_adc);

#endif // CONTROLADOR_NTC_H