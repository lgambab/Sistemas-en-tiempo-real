#ifndef BUTTON_DRIVER_H
#define BUTTON_DRIVER_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Inicializa el GPIO y la interrupción del botón.
// Envía un mensaje a la cola 'button_queue' cuando se presiona.
void button_init(QueueHandle_t button_queue);

#endif // BUTTON_DRIVER_H