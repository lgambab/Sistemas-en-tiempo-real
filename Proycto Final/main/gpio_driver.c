// gpio_driver.c
#include "gpio_driver.h"
#include "driver/gpio.h"

#define LED_YELLOW 2
#define LED_BLUE   5

void gpio_init_leds(void) {
    gpio_reset_pin(LED_YELLOW);
    gpio_reset_pin(LED_BLUE);
    gpio_set_direction(LED_YELLOW, GPIO_MODE_INPUT_OUTPUT);
    gpio_set_direction(LED_BLUE, GPIO_MODE_INPUT_OUTPUT);
}

void gpio_set_yellow(bool state) { gpio_set_level(LED_YELLOW, state); }
void gpio_set_blue(bool state)   { gpio_set_level(LED_BLUE, state); }
bool gpio_get_yellow(void)       { return gpio_get_level(LED_YELLOW); }
bool gpio_get_blue(void)         { return gpio_get_level(LED_BLUE); }