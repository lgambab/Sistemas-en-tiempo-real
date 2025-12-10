#ifndef MAIN_FAN_CONTROL_H_
#define MAIN_FAN_CONTROL_H_

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    FAN_MODE_MANUAL = 0,
    FAN_MODE_AUTO,
    FAN_MODE_REGISTERS   
} fan_mode_t;

void fan_control_init(void);

// Llamada desde HTTP (web)
void fan_control_set_mode(fan_mode_t mode);
fan_mode_t fan_control_get_mode(void);
void fan_control_set_manual_speed(uint8_t percent);
uint8_t fan_control_get_speed(void);

// Llamada periódicamente desde un task (si la usas)
void fan_control_update(void);

// Llamada periódica desde el comparador de registros:
void fan_control_on_register_tick(bool any_match_now);

#endif // MAIN_FAN_CONTROL_H_
