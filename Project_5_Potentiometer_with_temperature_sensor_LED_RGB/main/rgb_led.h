#ifndef RGB_LED_H
#define RGB_LED_H

#include <stdint.h>

void rgb_led_init(void);
void rgb_set_green_percent(uint8_t percent); // 0..100

#endif // RGB_LED_H
