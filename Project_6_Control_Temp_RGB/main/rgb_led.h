#ifndef RGB_LED_H
#define RGB_LED_H

#include <stdint.h>

// Pines del LED RGB
#define RGB_RED_PIN    13
#define RGB_GREEN_PIN  12
#define RGB_BLUE_PIN   25

// Configuración PWM
#define RGB_PWM_FREQ       5000    // 5 kHz
#define RGB_PWM_RESOLUTION 8       // 8 bits (0-255)

// Funciones públicas
void rgb_led_init(void);
void rgb_led_set_intensity(uint8_t intensity);  // 0-100%
void rgb_led_set_color(uint8_t red, uint8_t green, uint8_t blue);  // 0-255 cada color
void rgb_led_off(void);
uint8_t rgb_led_get_intensity(void);  // Obtener intensidad actual (0-100%)

#endif // RGB_LED_H
