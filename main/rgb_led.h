#ifndef RGB_LED_H
#define RGB_LED_H

#include "driver/ledc.h"
#include "esp_err.h"

// Estructura para manejar un LED RGB
typedef struct {
    int pin_r;
    int pin_g;
    int pin_b;
    ledc_channel_t channel_r;
    ledc_channel_t channel_g;
    ledc_channel_t channel_b;
    ledc_mode_t speed_mode;
    ledc_timer_t timer_sel;
} rgb_led_t;

// Inicializa el LED RGB con PWM
esp_err_t rgb_led_init(rgb_led_t *led);

// Ajusta el color (0-255 por componente)
esp_err_t rgb_led_set_color(rgb_led_t *led, uint8_t r, uint8_t g, uint8_t b);

#endif // RGB_LED_H
