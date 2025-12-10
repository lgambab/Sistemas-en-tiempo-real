#include "fan_control.h"
#include "fan_driver.h"


#include "esp_adc/adc_oneshot.h"
#include "ntc_driver.h"
#include "pir_driver.h"



extern adc_oneshot_unit_handle_t s_ntc_adc_handle;

static fan_mode_t s_mode = FAN_MODE_MANUAL;
static uint8_t    s_manual_speed = 0;
static uint8_t    s_current_speed = 0;  // Velocidad REAL actual del ventilador PWM
static bool       s_reg_active   = false;   // hay algún registro activo ahora



static uint8_t compute_auto_speed(float temp_c)
{
    /*
        Reglas:
        - < 25°C => 0%
        - 25–30°C => proporcional
        - > 30°C => 100%
    */

    if (temp_c < 25.0f) {
        return 0;
    }

    if (temp_c >= 30.0f) {
        return 100;
    }

    // Mapeo lineal 25 → 0%   30 → 100%
    float scale = (temp_c - 25.0f) / 5.0f;  // 0 a 1
    return (uint8_t)(scale * 100.0f);
}

void fan_control_init(void)
{
    fan_init();
    s_mode = FAN_MODE_MANUAL;
    s_manual_speed = 0;
    s_current_speed = 0;
    s_reg_active = false;
    fan_off();
}

void fan_control_set_mode(fan_mode_t mode)
{
    s_mode = mode;

    switch (s_mode) {

    case FAN_MODE_MANUAL:
        // usa directamente la velocidad del slider
        s_current_speed = s_manual_speed;
        fan_set_speed_percent(s_current_speed);
        break;

    case FAN_MODE_AUTO: {
        // Al cambiar a AUTO, aplicamos una primera vez lógica NTC+PIR
        if (s_ntc_adc_handle == NULL) {
            s_current_speed = 0;
            fan_off();
            break;
        }

        float temp_c = leer_temperatura_celsius(s_ntc_adc_handle);
        if (temp_c < -100.0f) { // error NTC
            s_current_speed = 0;
            fan_off();
            break;
        }

        // bool motion = pir_is_motion_active();
        // if (!motion) {
        //     s_current_speed = 0;
        //     fan_off();
        //     break;
        // }

        uint8_t spd = compute_auto_speed(temp_c);
        s_current_speed = spd;
        fan_set_speed_percent(s_current_speed);
        break;
    }

    case FAN_MODE_REGISTERS:
        if (s_reg_active) {
            s_current_speed = s_manual_speed;
            fan_set_speed_percent(s_current_speed);
        } else {
            s_current_speed = 0;
            fan_off();
        }
        break;
    }
}


fan_mode_t fan_control_get_mode(void)
{
    return s_mode;
}


void fan_control_set_manual_speed(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    s_manual_speed = percent;

    if (s_mode == FAN_MODE_MANUAL) {
        s_current_speed = s_manual_speed;
        fan_set_speed_percent(s_current_speed);
    }
}

uint8_t fan_control_get_speed(void)
{
    return s_current_speed;  // Devolver velocidad REAL del PWM, no del slider
}

void fan_control_on_register_tick(bool any_match_now)
{
    s_reg_active = any_match_now;

    // Solo tiene efecto en modo REGISTROS
    if (s_mode != FAN_MODE_REGISTERS) {
        return;
    }

    if (s_reg_active) {
        // Aquí decides a qué velocidad se enciende cuando un registro coincide.
        // Yo uso la velocidad del slider (s_manual_speed).
        s_current_speed = s_manual_speed;
        fan_set_speed_percent(s_current_speed);
    } else {
        // Ningún registro activo → ventilador apagado
        s_current_speed = 0;
        fan_off();
    }
}
void fan_control_update(void)
{
    if (s_mode != FAN_MODE_AUTO) {
        return;
    }

    if (s_ntc_adc_handle == NULL) {
        s_current_speed = 0;
        fan_off();
        return;
    }

    float temp_c = leer_temperatura_celsius(s_ntc_adc_handle);
    if (temp_c < -100.0f) { // error NTC
        s_current_speed = 0;
        fan_off();
        return;
    }

    // bool motion = pir_is_motion_active();
    // if (!motion) {
    //     s_current_speed = 0;
    //     fan_off();
    //     return;
    // }

    uint8_t auto_speed = compute_auto_speed(temp_c);
    s_current_speed = auto_speed;
    fan_set_speed_percent(s_current_speed);
}
