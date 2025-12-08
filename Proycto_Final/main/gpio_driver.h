// gpio_driver.h
#pragma once
#include <stdbool.h>

void gpio_init_leds(void);
void gpio_set_yellow(bool state);
void gpio_set_blue(bool state);
bool gpio_get_yellow(void);
bool gpio_get_blue(void);
