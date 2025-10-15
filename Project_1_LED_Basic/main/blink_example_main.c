/* LEDC (LED Controller) basic example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include "driver/ledc.h"
#include "esp_err.h"

#define LEDC_TIMER              LEDC_TIMER_0 //Se elige el timer 0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE //Se elige el modo de baja velocidad
#define LEDC_OUTPUT_IO          (2) // Define the output GPIO //Se elige el pin GPIO2 (D2 en la placa)
#define LEDC_CHANNEL            LEDC_CHANNEL_0 //Se elige el canal 0 (el canal es el que se usa para generar la señal PWM)
#define LEDC_DUTY_RES           LEDC_TIMER_13_BIT // Set duty resolution to 13 bits (ciclo de trabajo el cual define la intensidad de la luz)
#define LEDC_DUTY               (2048) // Set duty to 50%. (2 ** 13) * 50% = 4096 (Intensidad de la luz, siendo 8192 el máximo y 2048 el 25%)
#define LEDC_FREQUENCY          (4000) // Frequency in Hertz. Set frequency at 4 kHz (frecuencia de parpadeo de la luz, siendo 4kHz porque es un valor alto y no se nota el parpadeo)

/* Warning:
 * For ESP32, ESP32S2, ESP32S3, ESP32C3, ESP32C2, ESP32C6, ESP32H2 (rev < 1.2), ESP32P4 targets,
 * when LEDC_DUTY_RES selects the maximum duty resolution (i.e. value equal to SOC_LEDC_TIMER_BIT_WIDTH),
 * 100% duty cycle is not reachable (duty cannot be set to (2 ** SOC_LEDC_TIMER_BIT_WIDTH)).
 */

static void example_ledc_init(void)
{
    // Prepare and then apply the LEDC PWM timer configuration (Aqui se configura el timer que es el que genera la señal PWM en base a la configuración del canal)
    ledc_timer_config_t ledc_timer = { //Se configura el timer
        .speed_mode       = LEDC_MODE, //Acá se elige el modo de velocidad
        .duty_resolution  = LEDC_DUTY_RES, //Acá se elige la resolución del ciclo de trabajo (la resolución define la cantidad de niveles de intensidad de luz)
        .timer_num        = LEDC_TIMER, //Acá se elige el timer, el cual es el que se usa para generar la señal PWM
        .freq_hz          = LEDC_FREQUENCY  // Set output frequency at 4 kHz (frecuencia de parpadeo)
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer)); //Se aplica la configuración del timer

    // Prepare and then apply the LEDC PWM channel configuration (Aqui la configuración del canal es la que define el pin de salida y el canal)
    ledc_channel_config_t ledc_channel = {  //Se configura el canal
        .speed_mode     = LEDC_MODE, //Acá se elige el modo de velocidad
        .channel        = LEDC_CHANNEL, //Acá se elige el canal
        .timer_sel      = LEDC_TIMER, //Acá se elige el timer
        .intr_type      = LEDC_INTR_DISABLE, //Se deshabilitan las interrupciones
        .gpio_num       = LEDC_OUTPUT_IO, //Se elige el pin de salida
        .duty           = 0, // Set duty to 0% //Se inicia con el ciclo de trabajo en 0%
        .hpoint         = 0 //Se inicia el hpoint en 0 (no se usa en este ejemplo)
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel)); //Se aplica la configuración del canal
}

void app_main(void)
{
    // Set the LEDC peripheral configuration
    example_ledc_init(); //Se inicializa el LEDC
    // Set duty to 50%
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, LEDC_DUTY)); //Se configura el ciclo de trabajo al 50%
    // Update duty to apply the new value
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL)); //Se actualiza el ciclo de trabajo
}