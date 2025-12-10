#include "ntc_driver.h"


#include <math.h> // Librería matemática para cálculos como el logaritmo.

// --- CONFIGURACIÓN DEL SENSOR NTC ---
#define CANAL_ADC_NTC             ADC_CHANNEL_6 // Canal del ADC donde está conectado el NTC (GPIO 34).
#define RESISTENCIA_DIVISOR       10000.0f      // Resistencia fija (en Ohms) que acompaña al NTC.
#define RESISTENCIA_NOMINAL_NTC   10000.0f      // Resistencia del NTC a temperatura nominal (ej: 10k a 25°C).
#define TEMP_NOMINAL_NTC          25.0f         // Temperatura nominal en Celsius.
#define COEFICIENTE_BETA_NTC      3950.0f       // Coeficiente Beta del NTC (ver hoja de datos del sensor).
#define VOLTAJE_REFERENCIA_MV     3300.0f       // Voltaje de operación del microcontrolador (3.3V = 3300mV).


void inicializar_ntc(adc_oneshot_unit_handle_t manejador_adc) {
    // Configura el canal del ADC específico para el NTC.
    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN_DB_12,           // Atenuación para medir el rango completo de voltaje (0-3.3V).
        .bitwidth = ADC_BITWIDTH_DEFAULT,   // Resolución por defecto (normalmente 12 bits).
    };
    adc_oneshot_config_channel(manejador_adc, CANAL_ADC_NTC, &config);
}

float leer_temperatura_celsius(adc_oneshot_unit_handle_t manejador_adc) {
    int valor_adc_raw;
    // 1. Leer el valor digital puro del ADC (un número de 0 a 4095).
    adc_oneshot_read(manejador_adc, CANAL_ADC_NTC, &valor_adc_raw);

    // 2. Convertir el valor digital a milivoltios.
    float voltaje_mv = (valor_adc_raw * VOLTAJE_REFERENCIA_MV) / 4095.0f;

    // Si el voltaje es igual o mayor al de referencia, algo está mal (circuito abierto).
    if (voltaje_mv >= VOLTAJE_REFERENCIA_MV) return -999.0f; // Código de error.

    // 3. Calcular la resistencia actual del NTC usando la fórmula del divisor de voltaje.
    float resistencia_ntc = RESISTENCIA_DIVISOR * voltaje_mv / (VOLTAJE_REFERENCIA_MV - voltaje_mv);
    
    // Si la resistencia es cero o negativa, hay un error (cortocircuito).
    if (resistencia_ntc <= 0) return -999.0f; // Código de error.

    // 4. Aplicar la fórmula de Steinhart-Hart (versión simplificada con Beta) para obtener la temperatura.
    float steinhart = log(resistencia_ntc / RESISTENCIA_NOMINAL_NTC) / COEFICIENTE_BETA_NTC;
    steinhart += 1.0 / (TEMP_NOMINAL_NTC + 273.15); // Suma la inversa de la temperatura nominal en Kelvin.
    
    // 5. El resultado está en Kelvin, lo convertimos a Celsius.
    return (1.0 / steinhart) - 273.15;
}