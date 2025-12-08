#ifndef PIR_DRIVER_H
#define PIR_DRIVER_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include <stdbool.h>

/**
 * Estructura enviada por la cola cuando hay un evento del PIR.
 * motion = true  -> se detectó movimiento (salida PIR en 1)
 * motion = false -> el PIR volvió a 0 (opcional, según config)
 */
typedef struct {
    bool motion;
} pir_event_t;

/**
 * Inicializa el sensor PIR.
 *
 *  - pir_gpio: pin donde conectas la salida OUT del PIR.
 *  - pir_queue: cola donde se mandan eventos (puede ser NULL si no quieres cola).
 */
void pir_init(gpio_num_t pir_gpio, QueueHandle_t pir_queue);

/**
 * Lee el estado actual del pin del PIR (polling).
 *  - true  -> hay movimiento (pin en alto)
 *  - false -> no hay movimiento
 */
bool pir_is_motion_active(void);

#endif // PIR_DRIVER_H
