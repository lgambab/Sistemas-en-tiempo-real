/**
 * @file pir_driver.h
 * @brief Driver para sensor PIR (Passive Infrared) de detección de movimiento
 * @author Jair Hernan Telpis Cuaran, Luis Fernando Gamba Bedoya
 * @date 2025
 * @version 1.0
 * 
 * @details
 * Este módulo proporciona driver para sensores PIR HC-SR501 o compatibles.
 * El sensor detecta movimiento mediante cambios en la radiación infrarroja.
 * 
 * **Hardware:**
 * - Sensor PIR HC-SR501 (o compatible)
 * - Pin OUT conectado a GPIO 35 (configurable)
 * - VCC: 5V, GND: GND
 * - Salida digital: HIGH cuando detecta movimiento
 * 
 * **Modos de operación:**
 * 1. **Polling:** Llamar pir_is_motion_active() periódicamente
 * 2. **Interrupt:** Pasar una cola a pir_init() para recibir eventos
 * 
 * **Configuración del sensor HC-SR501:**
 * - Delay time: Ajusta cuánto tiempo permanece HIGH después de detección
 * - Sensitivity: Ajusta el rango de detección (3-7 metros típico)
 * - Trigger mode: Repeatable (H) o Non-repeatable (L)
 * 
 * @note Este driver soporta tanto ISR como polling
 * @see sensor_task.c Para integración con sistema de sensores
 */

#ifndef PIR_DRIVER_H
#define PIR_DRIVER_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include <stdbool.h>

/**
 * @struct pir_event_t
 * @brief Evento enviado por la cola cuando el PIR detecta cambio
 * 
 * @details
 * Esta estructura se envía a través de la cola cuando el sensor PIR
 * cambia de estado (flanco de subida o bajada).
 * 
 * **Uso:**
 * @code
 * QueueHandle_t pir_queue = xQueueCreate(10, sizeof(pir_event_t));
 * pir_init(GPIO_NUM_35, pir_queue);
 * 
 * pir_event_t evt;
 * if (xQueueReceive(pir_queue, &evt, portMAX_DELAY)) {
 *     if (evt.motion) {
 *         printf("Movimiento detectado!\n");
 *     } else {
 *         printf("Fin de movimiento\n");
 *     }
 * }
 * @endcode
 */
typedef struct {
    bool motion;  /**< true = movimiento detectado (pin HIGH), false = sin movimiento (pin LOW) */
} pir_event_t;

/**
 * @brief Inicializa el sensor PIR con ISR o polling
 * 
 * @details
 * Configura el GPIO como entrada y opcionalmente registra una ISR
 * para detectar cambios automáticamente.
 * 
 * **Configuración del GPIO:**
 * - Modo: INPUT
 * - Pull-up: Deshabilitado (sensor tiene pull-up interno)
 * - Pull-down: Deshabilitado
 * - Interrupción: Ambos flancos (ANYEDGE) para detectar inicio y fin
 * 
 * **Con cola (modo interrupt):**
 * - Cada cambio en el pin dispara ISR
 * - ISR envía evento a la cola
 * - Tarea puede esperar en xQueueReceive()
 * 
 * **Sin cola (modo polling):**
 * - Pasar NULL como pir_queue
 * - Llamar pir_is_motion_active() periódicamente
 * 
 * @param[in] pir_gpio Pin GPIO donde está conectado el sensor (ej: GPIO_NUM_35)
 * @param[in] pir_queue Cola para eventos (puede ser NULL para modo polling)
 * 
 * @warning Llamar solo una vez durante inicialización
 * @warning Si se usa cola, crearla antes con xQueueCreate()
 * 
 * @note Instala servicio ISR global si aún no está instalado
 * 
 * @see pir_is_motion_active() Para lectura directa sin cola
 */
void pir_init(gpio_num_t pir_gpio, QueueHandle_t pir_queue);

/**
 * @brief Lee el estado actual del sensor PIR (polling)
 * 
 * @details
 * Lee directamente el nivel lógico del GPIO configurado.
 * Útil para modo polling sin necesidad de ISR/cola.
 * 
 * **Interpretación:**
 * - true (HIGH): Sensor detecta movimiento ahora
 * - false (LOW): No hay movimiento detectado
 * 
 * **Uso recomendado:**
 * Llamar desde tarea periódica (cada 1-2 segundos) en modo polling,
 * o desde sensor_task para centralización thread-safe.
 * 
 * @return bool Estado del sensor
 * @retval true Movimiento detectado (pin en HIGH)
 * @retval false Sin movimiento (pin en LOW)
 * @retval false Si el sensor no ha sido inicializado
 * 
 * @note Thread-safe: solo lectura de GPIO
 * @note Tiempo de ejecución: <1μs
 * 
 * @see pir_init() Debe llamarse antes de usar esta función
 * @see sensor_task.c Integración con sistema de sensores
 */
bool pir_is_motion_active(void);

#endif // PIR_DRIVER_H
