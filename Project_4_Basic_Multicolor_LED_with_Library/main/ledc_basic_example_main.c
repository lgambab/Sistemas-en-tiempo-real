#include <stdio.h> // Cabeceras estándar para I/O
#include "rgb_led.h" // Cabecera del módulo RGB definido en este proyecto
#include "freertos/FreeRTOS.h" // API base de FreeRTOS
#include "freertos/task.h" // API de tareas de FreeRTOS

void app_main(void) // Función principal requerida por ESP-IDF
{ // Inicio de la función app_main
    rgb_led_t my_led = { // Inicializar la estructura de configuración del LED
        .pin_r = 5,  // Pin conectado al canal rojo
        .pin_g = 19, // Pin conectado al canal verde
        .pin_b = 23, // Pin conectado al canal azul
        .channel_r = LEDC_CHANNEL_0, // Canal PWM para rojo
        .channel_g = LEDC_CHANNEL_1, // Canal PWM para verde
        .channel_b = LEDC_CHANNEL_2, // Canal PWM para azul
        .speed_mode = LEDC_HIGH_SPEED_MODE, // Modo de velocidad LEDC
        .timer_sel = LEDC_TIMER_0 // Temporizador LEDC seleccionado
    }; // Fin de la inicialización de my_led

    rgb_led_init(&my_led); // Configurar hardware PWM y canales para el LED

    while (1) { // Bucle principal infinito
        rgb_led_set_color(&my_led, 255, 0, 0);  // Encender rojo al máximo
        vTaskDelay(pdMS_TO_TICKS(1000)); // Esperar 1 segundo
        rgb_led_set_color(&my_led, 0, 255, 0);  // Encender verde al máximo
        vTaskDelay(pdMS_TO_TICKS(1000)); // Esperar 1 segundo
        rgb_led_set_color(&my_led, 0, 0, 255);  // Encender azul al máximo
        vTaskDelay(pdMS_TO_TICKS(1000)); // Esperar 1 segundo
        rgb_led_set_color(&my_led, 255, 255, 0); // Mezcla para amarillo
        vTaskDelay(pdMS_TO_TICKS(1000)); // Esperar 1 segundo
        rgb_led_set_color(&my_led, 0, 0, 0); // Apagar todos los colores
        vTaskDelay(pdMS_TO_TICKS(1000)); // Esperar 1 segundo
    } // Fin del bucle principal
} // Fin de app_main
