/**
 * @file fan_control.h
 * @brief Control automático del ventilador con múltiples modos de operación
 * @author Jair Hernan Telpis Cuaran, Luis Fernando Gamba Bedoya
 * @date 2025
 * @version 1.0.0
 * 
 * @details Implementa la lógica de control del ventilador con tres modos:
 * - MANUAL: Control directo por slider de la interfaz web
 * - AUTO: Control automático basado en temperatura del sensor NTC
 * - REGISTERS: Control programado mediante registros horarios
 * 
 * Universidad Nacional de Colombia - Curso RTOS
 */

#ifndef MAIN_FAN_CONTROL_H_
#define MAIN_FAN_CONTROL_H_

#include <stdint.h>
#include <stdbool.h>

/**
 * @enum fan_mode_t
 * @brief Modos de operación del ventilador
 */
typedef enum {
    FAN_MODE_MANUAL = 0,    /**< Control manual mediante slider web */
    FAN_MODE_AUTO,          /**< Control automático por temperatura NTC */
    FAN_MODE_REGISTERS      /**< Control programado por registros horarios */
} fan_mode_t;

/** @brief Inicializar sistema de control del ventilador */
void fan_control_init(void);

/**
 * @brief Establecer modo de operación del ventilador
 * @param mode Nuevo modo (MANUAL, AUTO o REGISTERS)
 */
void fan_control_set_mode(fan_mode_t mode);

/**
 * @brief Obtener modo de operación actual
 * @return fan_mode_t Modo actual del ventilador
 */
fan_mode_t fan_control_get_mode(void);

/**
 * @brief Establecer velocidad manual del ventilador
 * @param percent Velocidad en porcentaje (0-100)
 */
void fan_control_set_manual_speed(uint8_t percent);

/**
 * @brief Obtener velocidad actual del PWM
 * @return uint8_t Velocidad en porcentaje (0-100)
 */
uint8_t fan_control_get_speed(void);

/**
 * @brief Actualizar control (llamado cada 1s por fan_task)
 * @details En modo AUTO ajusta velocidad según temperatura NTC
 */
void fan_control_update(void);

/**
 * @brief Notificar cambio en registros programados
 * @param any_match_now true si algún registro está activo
 */
void fan_control_on_register_tick(bool any_match_now);

/**
 * @brief Establecer umbrales de temperatura para modo AUTO
 * @param temp_min Temperatura mínima en °C (por debajo = ventilador apagado)
 * @param temp_max Temperatura máxima en °C (por encima = velocidad 100%)
 */
void fan_control_set_temp_thresholds(float temp_min, float temp_max);

/**
 * @brief Obtener umbrales de temperatura configurados
 * @param temp_min Puntero para almacenar temperatura mínima (puede ser NULL)
 * @param temp_max Puntero para almacenar temperatura máxima (puede ser NULL)
 */
void fan_control_get_temp_thresholds(float *temp_min, float *temp_max);

#endif // MAIN_FAN_CONTROL_H_
