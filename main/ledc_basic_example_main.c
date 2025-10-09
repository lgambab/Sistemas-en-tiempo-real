#include <stdio.h>
#include "rgb_led.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void)
{
    rgb_led_t my_led = {
        .pin_r = 25,  // Pines donde conectas R, G, B
        .pin_g = 26,
        .pin_b = 27,
        .channel_r = LEDC_CHANNEL_0,
        .channel_g = LEDC_CHANNEL_1,
        .channel_b = LEDC_CHANNEL_2,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .timer_sel = LEDC_TIMER_0
    };

    rgb_led_init(&my_led);

    while (1) {
        rgb_led_set_color(&my_led, 255, 0, 0);  // Rojo
        vTaskDelay(pdMS_TO_TICKS(1000));
        rgb_led_set_color(&my_led, 0, 255, 0);  // Verde
        vTaskDelay(pdMS_TO_TICKS(1000));
        rgb_led_set_color(&my_led, 0, 0, 255);  // Azul
        vTaskDelay(pdMS_TO_TICKS(1000));
        rgb_led_set_color(&my_led, 255, 255, 0); // Amarillo
        vTaskDelay(pdMS_TO_TICKS(1000));
        rgb_led_set_color(&my_led, 0, 0, 0); // Apagar
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
