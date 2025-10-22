#ifndef BUTTON_CONTROL_H
#define BUTTON_CONTROL_H

#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

// --- Configuración del Botón ---
#define BUTTON_PIN             14              // Botón en GPIO14
#define BUTTON_DEBOUNCE_TIME   50              // Tiempo de debounce en ms
#define BUTTON_LONG_PRESS_TIME 1000            // Tiempo para pulsación larga en ms

// --- Estados del Botón ---
typedef enum {
    BUTTON_EVENT_NONE = 0,
    BUTTON_EVENT_PRESSED,
    BUTTON_EVENT_RELEASED,
    BUTTON_EVENT_LONG_PRESS
} button_event_t;

// --- Estructura de datos del botón ---
typedef struct {
    bool is_pressed;
    bool print_enabled;
    uint32_t press_start_time;
    uint32_t last_event_time;
} button_state_t;

// --- Cola de eventos del botón ---
extern QueueHandle_t button_queue;

// --- Funciones públicas ---
void button_control_init(void);
void button_task(void *arg);
bool is_print_enabled(void);
void set_print_enabled(bool enabled);
button_state_t get_button_state(void);

#endif // BUTTON_CONTROL_H
