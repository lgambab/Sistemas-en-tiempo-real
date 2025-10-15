#include <stdio.h>
#include "freertos/FreeRTOS.h" // Biblioteca principal del sistema operativo FreeRTOS para delays.
#include "freertos/task.h"      // Necesaria para usar la función de delay vTaskDelay.
#include "driver/gpio.h"       // Biblioteca para controlar los pines GPIO del ESP32.

// --- Definición de los pines ---
// Aquí asignamos un nombre a cada número de pin que estamos usando.
#define RED_PIN   GPIO_NUM_5 // El pin Rojo (R) del LED está conectado al GPIO 15.
#define GREEN_PIN GPIO_NUM_19 // El pin Verde (G) del LED está conectado al GPIO 19.
#define BLUE_PIN  GPIO_NUM_23 // El pin Azul (B) del LED está conectado al GPIO 23.
#define BLINK_DELAY_MS 1000   // Definimos el tiempo de parpadeo en milisegundos (1000 ms = 1 segundo).

// --- Función principal del programa ---
// En ESP-IDF, todo comienza en la función app_main().
void app_main(void)
{
    // --- Configuración de los pines ---
    // Reseteamos los pines a su estado por defecto para asegurar una configuración limpia.
    gpio_reset_pin(RED_PIN);
    gpio_reset_pin(GREEN_PIN);
    gpio_reset_pin(BLUE_PIN);

    // Configuramos cada pin como una salida digital.
    gpio_set_direction(RED_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(GREEN_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(BLUE_PIN, GPIO_MODE_OUTPUT);

    // --- Bucle infinito para el parpadeo ---
    // El while(1) crea un bucle que se repetirá para siempre.
    while (1)
    {
        printf("Encendiendo LED Rojo\n"); // Imprime un mensaje en la consola serial.
        // --- Secuencia para el color ROJO ---
        gpio_set_level(RED_PIN, 1);     // Pone el pin del LED rojo en alto (1) para encenderlo.
        vTaskDelay(BLINK_DELAY_MS / portTICK_PERIOD_MS); // Espera la cantidad de tiempo definida arriba.
        gpio_set_level(RED_PIN, 0);     // Pone el pin en bajo (0) para apagarlo.

        printf("Encendiendo LED Verde\n");
        // --- Secuencia para el color VERDE ---
        gpio_set_level(GREEN_PIN, 1);   // Enciende el LED verde.
        vTaskDelay(BLINK_DELAY_MS / portTICK_PERIOD_MS); // Espera.
        gpio_set_level(GREEN_PIN, 0);   // Apaga el LED verde.
        
        printf("Encendiendo LED Azul\n");
        // --- Secuencia para el color AZUL ---
        gpio_set_level(BLUE_PIN, 1);    // Enciende el LED azul.
        vTaskDelay(BLINK_DELAY_MS / portTICK_PERIOD_MS); // Espera.
        gpio_set_level(BLUE_PIN, 0);    // Apaga el LED azul.

        // Una pequeña pausa antes de repetir toda la secuencia.
        vTaskDelay(BLINK_DELAY_MS / portTICK_PERIOD_MS);
    }
}