#ifndef UART_COMMANDS_H
#define UART_COMMANDS_H

#include <stdint.h>

// Estructura para thresholds de temperatura
typedef struct {
    float r_min, r_max;  // Rojo: 0-15°C
    float g_min, g_max;  // Verde: 10-30°C  
    float b_min, b_max;  // Azul: 40-50°C
} rgb_thresholds_t;

// Funciones públicas
void uart_commands_init(void);
void uart_commands_task(void *arg);
rgb_thresholds_t* get_rgb_thresholds(void);
void update_rgb_by_temperature(float temperature);

#endif // UART_COMMANDS_H
