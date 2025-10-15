#include "rgb_led.h" // Cabecera con tipos y prototipos para el LED RGB

esp_err_t rgb_led_init(rgb_led_t *led) // Inicializa temporizador y canales LEDC
{ // Inicio de rgb_led_init
    ledc_timer_config_t timer_conf = { // Configuración del temporizador LEDC
        .speed_mode       = led->speed_mode, // Modo de velocidad tomado de la estructura
        .timer_num        = led->timer_sel, // Número de timer a usar
        .duty_resolution  = LEDC_TIMER_8_BIT, // Resolución de 8 bits para duty
        .freq_hz          = 5000, // Frecuencia PWM: 5 kHz
        .clk_cfg          = LEDC_AUTO_CLK // Selección automática de reloj
    }; // Fin de la estructura timer_conf
    ESP_ERROR_CHECK(ledc_timer_config(&timer_conf)); // Aplicar configuración del timer

    // Configurar cada canal (R, G, B)
    ledc_channel_config_t channels[3] = { // Array con las 3 configuraciones de canal
        {
            .gpio_num   = led->pin_r, // Pin GPIO para el canal rojo
            .speed_mode = led->speed_mode, // Modo de velocidad para el canal
            .channel    = led->channel_r, // Canal LEDC para rojo
            .timer_sel  = led->timer_sel, // Timer asociado a este canal
            .duty       = 0, // Duty inicial 0 (apagado)
            .hpoint     = 0 // HPoint por defecto
        },
        {
            .gpio_num   = led->pin_g, // Pin GPIO para el canal verde
            .speed_mode = led->speed_mode, // Modo de velocidad para el canal
            .channel    = led->channel_g, // Canal LEDC para verde
            .timer_sel  = led->timer_sel, // Timer asociado a este canal
            .duty       = 0, // Duty inicial 0 (apagado)
            .hpoint     = 0 // HPoint por defecto
        },
        {
            .gpio_num   = led->pin_b, // Pin GPIO para el canal azul
            .speed_mode = led->speed_mode, // Modo de velocidad para el canal
            .channel    = led->channel_b, // Canal LEDC para azul
            .timer_sel  = led->timer_sel, // Timer asociado a este canal
            .duty       = 0, // Duty inicial 0 (apagado)
            .hpoint     = 0 // HPoint por defecto
        }
    }; // Fin del array channels

    for (int i = 0; i < 3; i++) { // Iterar sobre los 3 canales
        ESP_ERROR_CHECK(ledc_channel_config(&channels[i])); // Configurar cada canal
    } // Fin del bucle de configuración

    return ESP_OK; // Devolver éxito
} // Fin de rgb_led_init

esp_err_t rgb_led_set_color(rgb_led_t *led, uint8_t r, uint8_t g, uint8_t b) // Ajusta RGB
{ // Inicio de rgb_led_set_color
    // Como es cátodo común, un valor mayor en duty = más brillo
    ESP_ERROR_CHECK(ledc_set_duty(led->speed_mode, led->channel_r, r)); // Fijar duty rojo
    ESP_ERROR_CHECK(ledc_update_duty(led->speed_mode, led->channel_r)); // Aplicar duty rojo

    ESP_ERROR_CHECK(ledc_set_duty(led->speed_mode, led->channel_g, g)); // Fijar duty verde
    ESP_ERROR_CHECK(ledc_update_duty(led->speed_mode, led->channel_g)); // Aplicar duty verde

    ESP_ERROR_CHECK(ledc_set_duty(led->speed_mode, led->channel_b, b)); // Fijar duty azul
    ESP_ERROR_CHECK(ledc_update_duty(led->speed_mode, led->channel_b)); // Aplicar duty azul

    return ESP_OK; // Devolver éxito
} // Fin de rgb_led_set_color
