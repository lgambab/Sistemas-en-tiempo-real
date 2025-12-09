// peripherals.h
#ifndef PERIPHERALS_H
#define PERIPHERALS_H

#include "esp_err.h"

esp_err_t peripherals_init(void);
void peripherals_deinit(void);

// Show text on OLED (if present). Non-blocking; returns ESP_ERR_NOT_SUPPORTED if no driver.
esp_err_t peripherals_oled_show_text(const char* text);

// Send a key press into the system (helper for tests)
void peripherals_send_key_event(char key);

#endif // PERIPHERALS_H
