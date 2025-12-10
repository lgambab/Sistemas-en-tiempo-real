/**
 * @file ntc_driver.h
 * @brief Driver para sensor de temperatura NTC (Negative Temperature Coefficient)
 * @author Jair Hernan Telpis Cuaran, Luis Fernando Gamba Bedoya
 * @date 2025
 * @version 1.0
 * 
 * @details
 * Este módulo implementa el driver para leer temperatura mediante un termistor NTC 10KΩ
 * conectado en configuración divisor de voltaje con el ADC del ESP32.
 * 
 * **Hardware:**
 * - Termistor NTC 10KΩ @ 25°C (Beta = 3950K)
 * - Conectado a GPIO 34 (ADC1 Canal 6)
 * - Resistencia divisor: 10KΩ
 * - Rango de medición: -40°C a 125°C
 * 
 * **Ecuación de conversión:**
 * Se utiliza la ecuación simplificada de Steinhart-Hart:
 * @code
 * 1/T = 1/T0 + (1/B) * ln(R/R0)
 * @endcode
 * Donde:
 * - T = Temperatura absoluta en Kelvin
 * - T0 = Temperatura nominal (298.15K = 25°C)
 * - B = Coeficiente Beta (3950K)
 * - R = Resistencia actual del NTC
 * - R0 = Resistencia nominal (10kΩ)
 * 
 * **Manejo de errores:**
 * - Retorna -999.0f en caso de circuito abierto o cortocircuito
 * - Verifica voltajes fuera de rango (>3.3V o <0V)
 * - Valida resistencias negativas o nulas
 */

#ifndef CONTROLADOR_NTC_H
#define CONTROLADOR_NTC_H

#include "esp_adc/adc_oneshot.h" // Librería para usar el conversor Analógico-Digital (ADC).

/**
 * @brief Inicializa el canal ADC para el sensor NTC
 * 
 * @details
 * Configura el canal ADC_CHANNEL_6 (GPIO 34) con las siguientes características:
 * - Atenuación: 12dB (permite medir 0-3.3V)
 * - Resolución: 12 bits (valores 0-4095)
 * - Modo: Oneshot (lectura bajo demanda)
 * 
 * Esta función debe llamarse antes de leer_temperatura_celsius().
 * No es thread-safe - llamar solo desde una tarea o proteger con mutex.
 * 
 * @param[in] manejador_adc Handle del ADC previamente inicializado con adc_oneshot_new_unit()
 * 
 * @warning El manejador_adc debe ser válido. No verifica NULL.
 * @warning Llamar solo una vez durante la inicialización del sistema.
 * 
 * @see leer_temperatura_celsius()
 * @see adc_oneshot_config_channel() de ESP-IDF
 */
void inicializar_ntc(adc_oneshot_unit_handle_t manejador_adc);

/**
 * @brief Lee la temperatura actual del sensor NTC en grados Celsius
 * 
 * @details
 * Proceso de lectura y conversión:
 * 1. Lee valor ADC raw (0-4095)
 * 2. Convierte a voltaje: V = (ADC_raw * 3300mV) / 4095
 * 3. Calcula resistencia NTC: R_ntc = R_div * V / (V_ref - V)
 * 4. Aplica Steinhart-Hart: 1/T = 1/T0 + (1/B)*ln(R/R0)
 * 5. Convierte Kelvin a Celsius: T_c = T_k - 273.15
 * 
 * **Ecuación del divisor de voltaje:**
 * @code
 * V_out = V_in * (R_ntc / (R_div + R_ntc))
 * @endcode
 * 
 * **Precisión:**
 * - Resolución ADC: ~0.8mV (3300mV / 4095)
 * - Error típico: ±1°C en rango 0-50°C
 * - Error aumenta fuera del rango nominal
 * 
 * @param[in] manejador_adc Handle del ADC (debe estar inicializado)
 * 
 * @return float Temperatura en grados Celsius
 * @retval -999.0f Error de lectura (circuito abierto/cortocircuito)
 * @retval -40.0 a 125.0 Rango válido de temperatura
 * 
 * @warning Retorna -999.0f si:
 *          - Voltaje >= 3.3V (circuito abierto)
 *          - Resistencia <= 0 (cortocircuito o error de cálculo)
 * @warning El manejador_adc debe haberse inicializado con inicializar_ntc()
 * 
 * @note No es thread-safe. Si se llama desde múltiples tareas, proteger con mutex.
 * @note Tiempo de ejecución típico: ~100μs
 * 
 * @see inicializar_ntc()
 * @see sensor_task.c Uso desde tarea RTOS con mutex
 */
float leer_temperatura_celsius(adc_oneshot_unit_handle_t manejador_adc);

#endif // CONTROLADOR_NTC_H