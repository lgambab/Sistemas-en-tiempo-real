// fan_driver.h
#ifndef MAIN_FAN_DRIVER_H_
#define MAIN_FAN_DRIVER_H_

#include <stdint.h>
#include <stdbool.h>
#include "driver/ledc.h"

// GPIO y canal para el ventilador
#define FAN_PWM_GPIO        18        // cambiado a pin seguro (antes 21, conflicto con RGB_BLUE)
#define FAN_LEDC_CHANNEL    LEDC_CHANNEL_3
#define FAN_LEDC_MODE       LEDC_LOW_SPEED_MODE
#define FAN_LEDC_TIMER      LEDC_TIMER_1
#define FAN_LEDC_DUTY_RES   LEDC_TIMER_8_BIT   // 0-255
#define FAN_LEDC_FREQ_HZ    25000              // ~25 kHz

void fan_init(void);
void fan_set_speed_percent(uint8_t percent);   // 0-100
void fan_off(void);

#endif
