#include "rgb_led.h"

esp_err_t rgb_led_init(rgb_led_t *led)
{
    ledc_timer_config_t timer_conf = {
        .speed_mode       = led->speed_mode,
        .timer_num        = led->timer_sel,
        .duty_resolution  = LEDC_TIMER_8_BIT,
        .freq_hz          = 5000, // 5 kHz PWM
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

    // Configurar cada canal (R, G, B)
    ledc_channel_config_t channels[3] = {
        {
            .gpio_num   = led->pin_r,
            .speed_mode = led->speed_mode,
            .channel    = led->channel_r,
            .timer_sel  = led->timer_sel,
            .duty       = 0,
            .hpoint     = 0
        },
        {
            .gpio_num   = led->pin_g,
            .speed_mode = led->speed_mode,
            .channel    = led->channel_g,
            .timer_sel  = led->timer_sel,
            .duty       = 0,
            .hpoint     = 0
        },
        {
            .gpio_num   = led->pin_b,
            .speed_mode = led->speed_mode,
            .channel    = led->channel_b,
            .timer_sel  = led->timer_sel,
            .duty       = 0,
            .hpoint     = 0
        }
    };

    for (int i = 0; i < 3; i++) {
        ESP_ERROR_CHECK(ledc_channel_config(&channels[i]));
    }

    return ESP_OK;
}

esp_err_t rgb_led_set_color(rgb_led_t *led, uint8_t r, uint8_t g, uint8_t b)
{
    // Como es cátodo común, un valor mayor en duty = más brillo
    ESP_ERROR_CHECK(ledc_set_duty(led->speed_mode, led->channel_r, r));
    ESP_ERROR_CHECK(ledc_update_duty(led->speed_mode, led->channel_r));

    ESP_ERROR_CHECK(ledc_set_duty(led->speed_mode, led->channel_g, g));
    ESP_ERROR_CHECK(ledc_update_duty(led->speed_mode, led->channel_g));

    ESP_ERROR_CHECK(ledc_set_duty(led->speed_mode, led->channel_b, b));
    ESP_ERROR_CHECK(ledc_update_duty(led->speed_mode, led->channel_b));

    return ESP_OK;
}
