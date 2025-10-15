#ifndef RGB_LED_H // Guardas para evitar inclusiones múltiples
#define RGB_LED_H // Definición del guard

#include "driver/ledc.h" // Controlador LEDC (PWM) de ESP-IDF
#include "esp_err.h" // Tipos de error de ESP-IDF

// Estructura para manejar un LED RGB
typedef struct { // Definición de rgb_led_t
    int pin_r; // GPIO del componente rojo
    int pin_g; // GPIO del componente verde
    int pin_b; // GPIO del componente azul
    ledc_channel_t channel_r; // Canal LEDC para rojo
    ledc_channel_t channel_g; // Canal LEDC para verde
    ledc_channel_t channel_b; // Canal LEDC para azul
    ledc_mode_t speed_mode; // Modo de velocidad (high/low)
    ledc_timer_t timer_sel; // Temporizador LEDC seleccionado
} rgb_led_t; // Tipo de dato para representar el LED RGB

// Inicializa el LED RGB con PWM
esp_err_t rgb_led_init(rgb_led_t *led); // Prototipo: configurar timers y canales

// Ajusta el color (0-255 por componente)
esp_err_t rgb_led_set_color(rgb_led_t *led, uint8_t r, uint8_t g, uint8_t b); // Prototipo: fijar duty

#endif // RGB_LED_H - fin del guard de inclusión
