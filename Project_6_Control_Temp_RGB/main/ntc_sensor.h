#ifndef NTC_SENSOR_H
#define NTC_SENSOR_H

#include <stdint.h>

// --- Configuración de Pines ---
#define NTC_PIN         ADC_CHANNEL_9   // GPIO26 es ADC_CHANNEL_9
#define ADC_UNIT        ADC_UNIT_2

// --- Constantes del Termistor NTC 10k ---
#define NOMINAL_RESISTANCE      10000.0 // Resistencia nominal a 25°C
#define NOMINAL_TEMPERATURE     25.0    // Temperatura nominal en Celsius
#define B_COEFFICIENT           3380.0  // Coeficiente Beta del termistor (ajustado para NTC 10k)
#define SERIES_RESISTOR         10000.0 // Valor de la resistencia en serie (10k)


// Estructura para datos del sensor NTC
typedef struct {
    float temperature_c;
    float resistance;
    int raw_adc_value;
} ntc_data_t;

// Funciones públicas
void ntc_sensor_init(void);
ntc_data_t ntc_read_temperature(void);

#endif // NTC_SENSOR_H
