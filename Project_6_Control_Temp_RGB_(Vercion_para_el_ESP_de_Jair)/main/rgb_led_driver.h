#ifndef RGB_LED_DRIVER_H
#define RGB_LED_DRIVER_H

#include "driver/ledc.h"

// Inicializa el periférico LEDC para un LED RGB
void rgb_led_init(void);

// Establece el color del LED usando valores de duty cycle (0-1023 para 10 bits)
void rgb_led_set_color(uint32_t red_duty, uint32_t green_duty, uint32_t blue_duty);

#endif // RGB_LED_DRIVER_H