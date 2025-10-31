#include "rgb_led_driver.h"
#include "esp_err.h"

#define LED_RED_GPIO      10
#define LED_GREEN_GPIO    9
#define LED_BLUE_GPIO     46
#define LEDC_TIMER        LEDC_TIMER_0
#define LEDC_MODE         LEDC_LOW_SPEED_MODE
#define LEDC_RED_CH       LEDC_CHANNEL_0
#define LEDC_GREEN_CH     LEDC_CHANNEL_1
#define LEDC_BLUE_CH      LEDC_CHANNEL_2
#define LEDC_RESOLUTION   LEDC_TIMER_10_BIT

void rgb_led_init(void) {
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .duty_resolution  = LEDC_RESOLUTION,
        .timer_num        = LEDC_TIMER,
        .freq_hz          = 5000,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel[3] = {
        { .gpio_num = LED_RED_GPIO, .speed_mode = LEDC_MODE, .channel = LEDC_RED_CH, .timer_sel = LEDC_TIMER, .duty = 0, .hpoint = 0 },
        { .gpio_num = LED_GREEN_GPIO, .speed_mode = LEDC_MODE, .channel = LEDC_GREEN_CH, .timer_sel = LEDC_TIMER, .duty = 0, .hpoint = 0 },
        { .gpio_num = LED_BLUE_GPIO, .speed_mode = LEDC_MODE, .channel = LEDC_BLUE_CH, .timer_sel = LEDC_TIMER, .duty = 0, .hpoint = 0 },
    };

    for (int i = 0; i < 3; i++) {
        ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel[i]));
    }
}

void rgb_led_set_color(uint32_t red_duty, uint32_t green_duty, uint32_t blue_duty) {
    ledc_set_duty(LEDC_MODE, LEDC_RED_CH, red_duty);
    ledc_update_duty(LEDC_MODE, LEDC_RED_CH);

    ledc_set_duty(LEDC_MODE, LEDC_GREEN_CH, green_duty);
    ledc_update_duty(LEDC_MODE, LEDC_GREEN_CH);

    ledc_set_duty(LEDC_MODE, LEDC_BLUE_CH, blue_duty);
    ledc_update_duty(LEDC_MODE, LEDC_BLUE_CH);
}