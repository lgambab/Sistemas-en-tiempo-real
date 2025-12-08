#ifndef MAIN_FAN_CONTROL_H_
#define MAIN_FAN_CONTROL_H_

#include <stdint.h>

typedef enum {
    FAN_MODE_MANUAL = 0,
    FAN_MODE_AUTO,
    FAN_MODE_REGISTERS,
} fan_mode_t;

void fan_control_init(void);

// Llamada desde HTTP (web)
void fan_control_set_mode(fan_mode_t mode);
void fan_control_set_manual_speed(uint8_t percent);

// Llamada periódicamente desde un task (por ahora opcional)
void fan_control_update(void);

#endif // MAIN_FAN_CONTROL_H_
