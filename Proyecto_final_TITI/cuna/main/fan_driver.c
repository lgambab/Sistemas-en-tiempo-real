// fan_driver.c
#include "fan_driver.h"

static bool s_fan_initialized = false;

void fan_init(void)
{
    if (s_fan_initialized) return;

    ledc_timer_config_t ledc_timer = {
        .duty_resolution = FAN_LEDC_DUTY_RES,
        .freq_hz         = FAN_LEDC_FREQ_HZ,
        .speed_mode      = FAN_LEDC_MODE,
        .timer_num       = FAN_LEDC_TIMER,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .channel    = FAN_LEDC_CHANNEL,
        .duty       = 0,
        .hpoint     = 0,
        .gpio_num   = FAN_PWM_GPIO,
        .intr_type  = LEDC_INTR_DISABLE,
        .speed_mode = FAN_LEDC_MODE,
        .timer_sel  = FAN_LEDC_TIMER,
    };
    ledc_channel_config(&ledc_channel);

    s_fan_initialized = true;
}

void fan_set_speed_percent(uint8_t percent)
{
    if (!s_fan_initialized) fan_init();
    if (percent > 100) percent = 100;

    uint32_t duty = (255 * percent) / 100;
    ledc_set_duty(FAN_LEDC_MODE, FAN_LEDC_CHANNEL, duty);
    ledc_update_duty(FAN_LEDC_MODE, FAN_LEDC_CHANNEL);
}

void fan_off(void)
{
    fan_set_speed_percent(0);
}
