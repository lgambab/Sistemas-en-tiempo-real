#include "fan_control.h"
#include "fan_driver.h"

static fan_mode_t s_mode = FAN_MODE_MANUAL;
static uint8_t    s_manual_speed = 0;

void fan_control_init(void)
{
    fan_init();
    s_mode = FAN_MODE_MANUAL;
    s_manual_speed = 0;
    fan_off();
}

void fan_control_set_mode(fan_mode_t mode)
{
    s_mode = mode;
}

void fan_control_set_manual_speed(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    s_manual_speed = percent;

    if (s_mode == FAN_MODE_MANUAL) {
        fan_set_speed_percent(s_manual_speed);
    }
}

uint8_t fan_control_get_speed(void)
{
    return s_manual_speed;
}

void fan_control_update(void)
{
    switch (s_mode) {
    case FAN_MODE_MANUAL:
        fan_set_speed_percent(s_manual_speed);
        break;

    case FAN_MODE_AUTO:
        // De momento lo dejamos simple: por ahora igual que manual
        // Luego aquí metes NTC + PIR + reglas de temperatura.
        fan_set_speed_percent(s_manual_speed);
        break;

    case FAN_MODE_REGISTERS:
        // Aquí, más adelante, vas a meter la lógica de registros.
        fan_set_speed_percent(s_manual_speed);
        break;
    }
}
