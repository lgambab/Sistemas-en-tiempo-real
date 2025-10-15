#include "rgb_led.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "RGB_LED";

// GPIO y configuración
#define GPIO_GREEN      26
#define LEDC_TIMER      LEDC_TIMER_0
#define LEDC_MODE       LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL_G  LEDC_CHANNEL_1
#define LEDC_DUTY_RES   LEDC_TIMER_8_BIT // resolución 8 bits -> 0..255
#define LEDC_FREQUENCY  5000             // Hz

void rgb_led_init(void)
{
    // Timer config
    ledc_timer_config_t ledc_timer = {
        .duty_resolution = LEDC_DUTY_RES,
        .freq_hz = LEDC_FREQUENCY,
        .speed_mode = LEDC_MODE,
        .timer_num = LEDC_TIMER,
        .clk_cfg = LEDC_AUTO_CLK
    };
    esp_err_t err = ledc_timer_config(&ledc_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config failed: %d", err);
    }

    // Channel config (verde)
    ledc_channel_config_t ledc_channel = {
        .channel    = LEDC_CHANNEL_G,
        .duty       = 0,
        .gpio_num   = GPIO_GREEN,
        .speed_mode = LEDC_MODE,
        .hpoint     = 0,
        .timer_sel  = LEDC_TIMER
    };
    err = ledc_channel_config(&ledc_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_channel_config failed: %d", err);
    }

    ESP_LOGI(TAG, "RGB LED inicializado (verde GPIO %d)", GPIO_GREEN);
}

void rgb_set_green_percent(uint8_t percent)
{
    if (percent > 100) percent = 100;
    // Mapeo a duty 0..255 (8-bit)
    uint32_t duty = (percent * 255) / 100;
    // Si tu LED es common anode, invierte aquí: duty = 255 - duty;
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_G, duty);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_G);
}
