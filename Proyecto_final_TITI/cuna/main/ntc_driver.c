/**
 * @file ntc_driver.c
 * @brief Implementación del driver para sensor NTC con conversión Steinhart-Hart
 * @author Jair Hernan Telpis Cuaran, Luis Fernando Gamba Bedoya
 * @date 2025
 * 
 * @details
 * Implementa la lectura del termistor NTC mediante ADC y conversión a temperatura
 * usando la ecuación simplificada de Steinhart-Hart con coeficiente Beta.
 * 
 * **Configuración de hardware:**
 * - NTC 10KΩ @ 25°C conectado a GPIO 34
 * - Divisor de voltaje: R_div = 10KΩ
 * - VCC = 3.3V
 * - ADC: 12 bits, atenuación 12dB
 * 
 * **Circuito equivalente:**
 * @code
 *   VCC (3.3V)
 *      |
 *    [R_div = 10KΩ]
 *      |
 *      +-----> GPIO 34 (ADC1_CH6)
 *      |
 *    [R_ntc = f(T)]
 *      |
 *    GND
 * @endcode
 */

#include "ntc_driver.h"


#include <math.h> // Librería matemática para cálculos como el logaritmo.

/**
 * @defgroup NTC_Config Configuración del Sensor NTC
 * @brief Parámetros de hardware y calibración del termistor
 * @{
 */

/** @brief Canal del ADC donde está conectado el NTC (GPIO 34) */
#define CANAL_ADC_NTC             ADC_CHANNEL_6

/** @brief Resistencia fija del divisor de voltaje (10kΩ) */
#define RESISTENCIA_DIVISOR       10000.0f

/** @brief Resistencia nominal del NTC a temperatura de referencia (10kΩ @ 25°C) */
#define RESISTENCIA_NOMINAL_NTC   10000.0f

/** @brief Temperatura nominal de calibración (25°C) */
#define TEMP_NOMINAL_NTC          25.0f

/** 
 * @brief Coeficiente Beta del NTC (3950K)
 * @details 
 * El coeficiente Beta caracteriza la variación de resistencia con temperatura.
 * Se obtiene de la hoja de datos del fabricante. Un Beta típico para NTC 10K
 * es 3950K ± 1%.
 * 
 * La ecuación relacionada es:
 * @code
 * R(T) = R0 * exp(B * (1/T - 1/T0))
 * @endcode
 */
#define COEFICIENTE_BETA_NTC      3950.0f

/** @brief Voltaje de referencia del ADC (3.3V = 3300mV) */
#define VOLTAJE_REFERENCIA_MV     3300.0f

/** @} */ // end of NTC_Config

/**
 * @brief Configura el canal ADC para el termistor NTC
 * @implements inicializar_ntc
 * 
 * @details
 * Establece la configuración del canal ADC con:
 * - Atenuación 12dB: rango completo 0-3.3V
 * - Resolución por defecto: 12 bits (0-4095)
 * 
 * La atenuación de 12dB es necesaria porque el divisor de voltaje produce
 * voltajes en todo el rango 0-3.3V dependiendo de la temperatura.
 * 
 * @param[in] manejador_adc Handle del ADC previamente inicializado
 */
void inicializar_ntc(adc_oneshot_unit_handle_t manejador_adc) {
    // Estructura de configuración del canal ADC
    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN_DB_12,           // Atenuación 12dB: rango 0-3.3V
        .bitwidth = ADC_BITWIDTH_DEFAULT,   // Resolución 12 bits (4096 niveles)
    };
    // Aplica la configuración al canal del NTC
    adc_oneshot_config_channel(manejador_adc, CANAL_ADC_NTC, &config);
}

/**
 * @brief Realiza lectura ADC y convierte a temperatura usando Steinhart-Hart
 * @implements leer_temperatura_celsius
 * 
 * @details
 * Algoritmo de conversión:
 * 1. **Lectura ADC:** Obtiene valor digital (0-4095)
 * 2. **Conversión a voltaje:** V = ADC_raw * (3300mV / 4095)
 * 3. **Cálculo de resistencia NTC:**
 *    @code
 *    R_ntc = R_div * V / (V_ref - V)
 *    @endcode
 * 4. **Ecuación Steinhart-Hart simplificada:**
 *    @code
 *    1/T = 1/T0 + (1/B) * ln(R_ntc / R0)
 *    @endcode
 * 5. **Conversión K → °C:** T_celsius = T_kelvin - 273.15
 * 
 * **Detección de errores:**
 * - V ≥ V_ref: Circuito abierto (NTC desconectado)
 * - R_ntc ≤ 0: Cortocircuito o error matemático
 * 
 * @param[in] manejador_adc Handle del ADC configurado
 * @return Temperatura en °C o -999.0f en caso de error
 */
float leer_temperatura_celsius(adc_oneshot_unit_handle_t manejador_adc) {
    int valor_adc_raw;
    
    // PASO 1: Leer valor digital del ADC (0 a 4095)
    adc_oneshot_read(manejador_adc, CANAL_ADC_NTC, &valor_adc_raw);

    // PASO 2: Convertir valor digital a voltaje analógico
    // Fórmula: V = (ADC / ADC_max) * V_ref
    float voltaje_mv = (valor_adc_raw * VOLTAJE_REFERENCIA_MV) / 4095.0f;

    // Validación: Detectar circuito abierto (NTC desconectado)
    // Si V ≥ V_ref, no hay caída de voltaje en R_div → circuito abierto
    if (voltaje_mv >= VOLTAJE_REFERENCIA_MV) {
        return -999.0f; // Código de error: circuito abierto
    }

    // PASO 3: Calcular resistencia del NTC usando divisor de voltaje
    // Del divisor de voltaje: V_out = V_in * (R_ntc / (R_div + R_ntc))
    // Despejando R_ntc: R_ntc = R_div * V_out / (V_in - V_out)
    float resistencia_ntc = RESISTENCIA_DIVISOR * voltaje_mv / (VOLTAJE_REFERENCIA_MV - voltaje_mv);
    
    // Validación: Detectar cortocircuito o error de cálculo
    if (resistencia_ntc <= 0) {
        return -999.0f; // Código de error: resistencia inválida
    }

    // PASO 4: Aplicar ecuación de Steinhart-Hart simplificada con Beta
    // Fórmula: 1/T = 1/T0 + (1/B) * ln(R/R0)
    // Donde: T y T0 en Kelvin, B en Kelvin, R y R0 en Ohms
    float steinhart = log(resistencia_ntc / RESISTENCIA_NOMINAL_NTC) / COEFICIENTE_BETA_NTC;
    steinhart += 1.0 / (TEMP_NOMINAL_NTC + 273.15); // Suma 1/T0 (T0 en Kelvin)
    
    // PASO 5: Convertir de Kelvin a Celsius
    // T_celsius = T_kelvin - 273.15
    // Nota: steinhart contiene 1/T, por eso se invierte primero
    return (1.0 / steinhart) - 273.15;
}