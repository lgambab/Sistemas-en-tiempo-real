// ===== INCLUDES Y CONFIGURACIÓN =====
#include "rgb_led.h"
#include "esp_log.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include <stdlib.h>

static const char *TAG = "RGB_LED";

// ===== CONFIGURACIÓN PWM =====
#define RGB_LEDC_TIMER      LEDC_TIMER_0
#define RGB_LEDC_MODE       LEDC_LOW_SPEED_MODE
#define RGB_LEDC_CHANNEL_R  LEDC_CHANNEL_0
#define RGB_LEDC_CHANNEL_G  LEDC_CHANNEL_1
#define RGB_LEDC_CHANNEL_B  LEDC_CHANNEL_2

// Variables globales para control de intensidad
static uint8_t current_intensity = 0;  // 0-100%
static uint8_t current_red = 255;      // Color base (blanco)
static uint8_t current_green = 255;
static uint8_t current_blue = 255;

// ===== FUNCIONES PRIVADAS =====
static void rgb_led_update_pwm(void)
{
    // Aplicar intensidad a los colores base
    uint8_t red_value = (current_red * current_intensity) / 100;
    uint8_t green_value = (current_green * current_intensity) / 100;
    uint8_t blue_value = (current_blue * current_intensity) / 100;
    
    // Actualizar PWM
    ESP_ERROR_CHECK(ledc_set_duty(RGB_LEDC_MODE, RGB_LEDC_CHANNEL_R, red_value));
    ESP_ERROR_CHECK(ledc_set_duty(RGB_LEDC_MODE, RGB_LEDC_CHANNEL_G, green_value));
    ESP_ERROR_CHECK(ledc_set_duty(RGB_LEDC_MODE, RGB_LEDC_CHANNEL_B, blue_value));
    
    // Aplicar cambios
    ESP_ERROR_CHECK(ledc_update_duty(RGB_LEDC_MODE, RGB_LEDC_CHANNEL_R));
    ESP_ERROR_CHECK(ledc_update_duty(RGB_LEDC_MODE, RGB_LEDC_CHANNEL_G));
    ESP_ERROR_CHECK(ledc_update_duty(RGB_LEDC_MODE, RGB_LEDC_CHANNEL_B));
}

// ===== FUNCIONES PÚBLICAS =====
void rgb_led_init(void)
{
    ESP_LOGI(TAG, "Inicializando LED RGB...");
    
    // Configurar timer
    ledc_timer_config_t ledc_timer = {
        .speed_mode = RGB_LEDC_MODE,
        .timer_num = RGB_LEDC_TIMER,
        .duty_resolution = RGB_PWM_RESOLUTION,
        .freq_hz = RGB_PWM_FREQ,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));
    
    // Configurar canal rojo
    ledc_channel_config_t ledc_channel_r = {
        .speed_mode = RGB_LEDC_MODE,
        .channel = RGB_LEDC_CHANNEL_R,
        .timer_sel = RGB_LEDC_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = RGB_RED_PIN,
        .duty = 0,
        .hpoint = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel_r));
    
    // Configurar canal verde
    ledc_channel_config_t ledc_channel_g = {
        .speed_mode = RGB_LEDC_MODE,
        .channel = RGB_LEDC_CHANNEL_G,
        .timer_sel = RGB_LEDC_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = RGB_GREEN_PIN,
        .duty = 0,
        .hpoint = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel_g));
    
    // Configurar canal azul
    ledc_channel_config_t ledc_channel_b = {
        .speed_mode = RGB_LEDC_MODE,
        .channel = RGB_LEDC_CHANNEL_B,
        .timer_sel = RGB_LEDC_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = RGB_BLUE_PIN,
        .duty = 0,
        .hpoint = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel_b));
    
    // Inicializar con LED apagado
    rgb_led_off();
    
    ESP_LOGI(TAG, "LED RGB inicializado correctamente");
    ESP_LOGI(TAG, "  - Rojo: GPIO%d", RGB_RED_PIN);
    ESP_LOGI(TAG, "  - Verde: GPIO%d", RGB_GREEN_PIN);
    ESP_LOGI(TAG, "  - Azul: GPIO%d", RGB_BLUE_PIN);
    ESP_LOGI(TAG, "  - Frecuencia PWM: %d Hz", RGB_PWM_FREQ);
}

void rgb_led_set_intensity(uint8_t intensity)
{
    if (intensity > 100) {
        intensity = 100;
    }
    
    current_intensity = intensity;
    rgb_led_update_pwm();
    
    // Sin logging individual - se mostrará en el sistema de monitoreo general
}

void rgb_led_set_color(uint8_t red, uint8_t green, uint8_t blue)
{
    current_red = red;
    current_green = green;
    current_blue = blue;
    
    rgb_led_update_pwm();
    
    // Sin logging individual - se mostrará en el sistema de monitoreo general
}

void rgb_led_off(void)
{
    current_intensity = 0;
    rgb_led_update_pwm();
    ESP_LOGI(TAG, "LED RGB apagado");
}

uint8_t rgb_led_get_intensity(void)
{
    return current_intensity;
}
