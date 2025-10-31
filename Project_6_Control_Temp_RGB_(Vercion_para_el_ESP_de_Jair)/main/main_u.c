#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"

#include "rgb_led_driver.h"
#include "ntc_driver.h"
#include "pot_driver.h"
#include "button_driver.h"

static const char *TAG = "MAIN_APP";

// --- Estructuras y Colas para comunicación ---
typedef struct {
    float temperatura_c;
    int pot_voltage_mv;
} SensorData_t;

typedef enum { CMD_SET_RED, CMD_SET_GREEN, CMD_SET_BLUE, CMD_SET_POT } CommandType_t;
typedef struct {
    CommandType_t type;
    float value1; // min_temp o color a controlar por el pot
    float value2; // max_temp
} AppCommand_t;

static QueueHandle_t sensor_data_queue;
static QueueHandle_t command_queue;
static QueueHandle_t button_press_queue;

static QueueHandle_t reprint_menu_queue;// 

static adc_oneshot_unit_handle_t adc1_handle;


// --- Prototipos de las Tareas ---
// Esto le dice al compilador que estas funciones existen y serán definidas más adelante.
void uart_receiver_task(void *pvParameters);
void sensor_reader_task(void *pvParameters);
void led_control_task(void *pvParameters);
void monitor_print_task(void *pvParameters);


void app_main(void) {
    // 1. Crear colas
    sensor_data_queue = xQueueCreate(1, sizeof(SensorData_t));
    command_queue = xQueueCreate(5, sizeof(AppCommand_t));
    button_press_queue = xQueueCreate(1, sizeof(uint32_t));

    reprint_menu_queue = xQueueCreate(1, sizeof(uint8_t)); // 

    // 2. Inicializar drivers
    rgb_led_init();

    adc_oneshot_unit_init_cfg_t init_config1 = { .unit_id = ADC_UNIT_1 };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));
    ntc_init(adc1_handle);
    pot_init(adc1_handle);

    button_init(button_press_queue);
    
    // Configurar UART para comandos
    uart_config_t uart_config = {
        .baud_rate = 115200, .data_bits = UART_DATA_8_BITS, .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1, .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(UART_NUM_0, 256 * 2, 0, 0, NULL, 0);
    uart_param_config(UART_NUM_0, &uart_config);
    
    ESP_LOGI(TAG, "Inicialización completa. Creando tareas.");

    // 3. Crear tareas
    xTaskCreate(uart_receiver_task, "UART Receiver", 2048, NULL, 5, NULL);
    xTaskCreate(sensor_reader_task, "Sensor Reader", 2048, NULL, 10, NULL);
    xTaskCreate(led_control_task, "LED Control", 4096, NULL, 5, NULL);
    xTaskCreate(monitor_print_task, "Monitor Print", 2048, NULL, 4, NULL);
}

// ========= IMPLEMENTACIÓN DE TAREAS =========

// Tarea 1: recibe comandos del UART
void uart_receiver_task(void *pvParameters) {
    uint8_t *data = (uint8_t *) malloc(256);
    while (1) {
        int len = uart_read_bytes(UART_NUM_0, data, 255, pdMS_TO_TICKS(20));
        if (len > 0) {
            data[len] = '\0';
            AppCommand_t cmd;
            char type_char;
            char pot_target[5];

            if (strncmp((char*)data, "help", 4) == 0) {
                uint8_t dummy_val = 1;
                xQueueSend(reprint_menu_queue, &dummy_val, 0);
            }

            // Interpretacion de comnados (R G B) Char float float ejemplo  R 0.5 1.0
            else if (sscanf((char*)data, "%c %f %f", &type_char, &cmd.value1, &cmd.value2) == 3) {
                bool send = true;

                // segun la letra recibida se calsifica el comando
                if (type_char == 'R' || type_char == 'r') cmd.type = CMD_SET_RED;
                else if (type_char == 'G' || type_char == 'g') cmd.type = CMD_SET_GREEN;
                else if (type_char == 'B' || type_char == 'b') cmd.type = CMD_SET_BLUE;
                else send = false;
                
                // se envia el comando a la cola command_queuw para que otra tarea lo ejecute
                if (send) xQueueSend(command_queue, &cmd, pdMS_TO_TICKS(10));
            
            // Interpretacion para comando 'pot' detecta si recibe comando como pot r, pot g, pot b
            // Asigna un numero (1,2,3) segun el canal del pot luego lo envia a la cola
            }
            
            else if (sscanf((char*)data, "pot %s", pot_target) == 1) {
                cmd.type = CMD_SET_POT;
                if (strcmp(pot_target, "r") == 0) cmd.value1 = 1;
                else if (strcmp(pot_target, "g") == 0) cmd.value1 = 2;
                else if (strcmp(pot_target, "b") == 0) cmd.value1 = 3;
                else cmd.value1 = 0; // none
                xQueueSend(command_queue, &cmd, pdMS_TO_TICKS(10));
            }
        }
    }
}
// Tarea 2: Lectura periodica de los sensores analogicos
void sensor_reader_task(void *pvParameters) {
    #define AVG_SAMPLES 10                                               // se define un promedio de 10 muestras
    float temp_history[AVG_SAMPLES] = {0};                               // guarda las ultimas 10 lecturas de temperatura
    int history_idx = 0;                                                 // indica la posicion donde se guiardara la siguiente lectura (y continua rotando)

    while (1) {
        // Leer sensores
        temp_history[history_idx] = ntc_read_celsius(adc1_handle);      // Llama a "ntc_read_celsius(adc1_handle)"" para leer la temp actual y guarda el valor en "temp_history[history_idx]"
        history_idx = (history_idx + 1) % AVG_SAMPLES;                  // Avanza el índice circularmente (% AVG_SAMPLES) para mantener solo las últimas 10 lecturas.

        // Calcular promedio para suavizar la temperatura de los ultimos 10 valores
        float temp_sum = 0;
        for (int i = 0; i < AVG_SAMPLES; i++) {
            temp_sum += temp_history[i];
        }

        SensorData_t data;
        data.temperatura_c = temp_sum / AVG_SAMPLES;                   // Esto da una temperatura promedio suavizada
        data.pot_voltage_mv = pot_read_mv(adc1_handle);                // Aquí llama a otra función (pot_read_mv) para leer el voltaje del potenciómetro en milivoltios.
        //Ese dato se guarda en el mismo struct SensorData_t

        // Sobrescribir la cola con el dato más reciente
        xQueueOverwrite(sensor_data_queue, &data);                    //Esto envía los datos más recientes a una cola 

        vTaskDelay(pdMS_TO_TICKS(200));                               // Detiene la tarea 200 ms, o sea, lee los sensores 5 veces por segundo (5 Hz)
    }
}

// Tarea 3: Da las ordenes

void led_control_task(void *pvParameters) {
    // --- Estado local de configuración ---
    float red_min = 0, red_max = 15;
    float green_min = 10, green_max = 30;
    float blue_min = 25, blue_max = 40;
    int pot_control_target = 0; // 0: none, 1: red, 2: green, 3: blue

    // --- Variables para mejoras de robustez ---
    SensorData_t current_sensors;
    AppCommand_t received_cmd;
    float smoothed_temp = -100.0f; // Inicializado a un valor inválido para la primera lectura
    const float EMA_ALPHA = 0.1f;  // Factor de suavizado (más bajo = más suave)
    const uint32_t MAX_DUTY = (1 << 10) - 1; // 1023
    bool has_valid_sensor_data = false;

    while (1) {
        // 1. Revisar si hay nuevos comandos (sin bloquear)
        if (xQueueReceive(command_queue, &received_cmd, 0) == pdPASS) {
            switch(received_cmd.type) {
                case CMD_SET_RED:
                    red_min = received_cmd.value1;
                    red_max = received_cmd.value2;
                    // MEJORA: Prevenir división por cero.
                    if (red_max <= red_min) red_max = red_min + 0.1f;
                    break;
                case CMD_SET_GREEN:
                    green_min = received_cmd.value1;
                    green_max = received_cmd.value2;
                    if (green_max <= green_min) green_max = green_min + 0.1f;
                    break;
                case CMD_SET_BLUE:
                    blue_min = received_cmd.value1;
                    blue_max = received_cmd.value2;
                    if (blue_max <= blue_min) blue_max = blue_min + 0.1f;
                    break;
                case CMD_SET_POT:
                    pot_control_target = (int)received_cmd.value1;
                    break;
            }
        }
        if (xQueueReceive(button_press_queue, &btn_gpio, 0) == pdPASS) {

            }
        
        // 2. MEJORA: Actuar solo si hay datos de sensor frescos.
        if (xQueuePeek(sensor_data_queue, &current_sensors, 0) == pdPASS) {
            has_valid_sensor_data = true;

            // MEJORA: Suavizar la temperatura con un Promedio Móvil Exponencial para evitar flicker.
            if (smoothed_temp < -99.0f) { // Si es la primera lectura válida
                smoothed_temp = current_sensors.temperatura_c;
            } else {
                smoothed_temp = (EMA_ALPHA * current_sensors.temperatura_c) + ((1.0f - EMA_ALPHA) * smoothed_temp);
            }
            
            // 3. Calcular el valor de cada color usando la temperatura suavizada.
            float temp = smoothed_temp;
            uint32_t red_duty = (temp <= red_min) ? 0 : (temp >= red_max) ? MAX_DUTY : (uint32_t)(MAX_DUTY * (temp - red_min) / (red_max - red_min));
            uint32_t green_duty = (temp <= green_min) ? 0 : (temp >= green_max) ? MAX_DUTY : (uint32_t)(MAX_DUTY * (temp - green_min) / (green_max - green_min));
            uint32_t blue_duty = (temp <= blue_min) ? 0 : (temp >= blue_max) ? MAX_DUTY : (uint32_t)(MAX_DUTY * (temp - blue_min) / (blue_max - blue_min));

            // 4. Sobrescribir con el valor del potenciómetro si está activado
            // MEJORA: Saturar el valor del potenciómetro para evitar desbordes.
            int pot_mv = current_sensors.pot_voltage_mv;
            if (pot_mv < 0) pot_mv = 0;
            if (pot_mv > 3300) pot_mv = 3300;
            uint32_t pot_duty = (uint32_t)(pot_mv * MAX_DUTY) / 3300;

            if (pot_control_target == 1) red_duty = pot_duty;
            if (pot_control_target == 2) green_duty = pot_duty;
            if (pot_control_target == 3) blue_duty = pot_duty;

            // 5. Actualizar el LED
            rgb_led_set_color(red_duty, green_duty, blue_duty);
        }

        // Ceder CPU. Si no hay datos válidos, esta tarea solo duerme y espera.
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/**
 * @brief Imprime el menú de comandos en la consola.
 *
 * Esta función es llamada cuando el sistema entra en modo de configuración
 * (es decir, cuando el monitoreo de sensores está desactivado).
 * Proporciona al usuario una lista de los comandos disponibles.
 */
static void print_menu(void) {
    // Se imprimen saltos de línea para separar visualmente el menú de logs anteriores.
    printf("\n\n=== MODO CONFIGURACIÓN ===\n");
    printf("El monitor de sensores está en pausa. Puedes enviar comandos:\n");
    printf("----------------------------------------------------------\n");
    printf("  R <min> <max>   -> Define el rango de temperatura para el ROJO (ej: R 0 15.5)\n");
    printf("  G <min> <max>   -> Define el rango de temperatura para el VERDE (ej: G 10 30)\n");
    printf("  B <min> <max>   -> Define el rango de temperatura para el AZUL (ej: B 25 40)\n");
    printf("  pot <target>    -> Asigna el potenciómetro a un color (r, g, b) o a ninguno (none)\n");
    printf("  help            -> Vuelve a mostrar este menú de ayuda\n");
    printf("----------------------------------------------------------\n");
    printf(">>> Presiona el BOTÓN FÍSICO para volver al modo monitor <<<\n\n");
    printf("> "); // Prompt para indicar que el sistema está esperando un comando.
    
    // fflush(stdout) es crucial. Fuerza al sistema a enviar inmediatamente todo lo que está
    // en el buffer de salida a la consola. Sin esto, el prompt '>' podría no aparecer
    // hasta que el usuario presione Enter.
    fflush(stdout);
}

//Tarea 4: Diagnostico y monitoreo


/**
 * @brief Tarea de Interfaz de Usuario (Monitor y Menú) - VERSIÓN SIMPLIFICADA
 *
 * Esta versión utiliza un bucle simple y un contador para gestionar la impresión,
 * garantizando que la revisión de eventos (botón, comandos) ocurra siempre
 * de forma rápida y predecible.
 */
void monitor_print_task(void *pvParameters) {
    bool is_monitoring = true;
    uint32_t btn_gpio;
    uint8_t dummy_notification;

    // Usaremos un contador simple para decidir cuándo imprimir.
    // Si nuestro delay es de 100ms, un contador de 20 ciclos equivale a 2 segundos.
    int print_countdown = 0;
    const int PRINT_INTERVAL_CYCLES = 20; // 20 ciclos * 100 ms/ciclo = 2000 ms

    printf("\nSistema inicializado. Modo Monitor: ACTIVADO\n");

    while (1) {
        // --- SECCIÓN 1: Revisar TODOS los eventos en cada ciclo ---
        // Estas revisiones son "no bloqueantes" (timeout de 0).

        // ¿Se presionó el botón?
        if (xQueueReceive(button_press_queue, &btn_gpio, 0) == pdPASS) {
            is_monitoring = !is_monitoring; // Cambiar de modo
            if (is_monitoring) {
                printf("\n\n--- Modo Monitor: ACTIVADO ---\n");
                print_countdown = 0; // Resetear el contador para imprimir de inmediato.
            } else {
                print_menu();
            }
        }

        // ¿Se pidió ayuda? (Solo si estamos en modo menú)
        if (!is_monitoring && xQueueReceive(reprint_menu_queue, &dummy_notification, 0) == pdPASS) {
             print_menu();
        }


        // --- SECCIÓN 2: Lógica del estado actual ---

        if (is_monitoring) {
            // Decrementamos el contador en cada ciclo.
            print_countdown--;

            // Si el contador llega a cero (o menos), es hora de imprimir.
            if (print_countdown <= 0) {
                SensorData_t data;
                if (xQueuePeek(sensor_data_queue, &data, 0) == pdPASS) {
                    printf("Temp: %.2f C | Pot: %d mV\n", data.temperatura_c, data.pot_voltage_mv);
                } else {
                    printf("[WARN] Esperando datos del sensor...\n");
                }
                // Reiniciamos el contador para la próxima impresión.
                print_countdown = PRINT_INTERVAL_CYCLES;
            }
        }


        // --- SECCIÓN 3: Delay unificado ---
        // Este es el corazón de la tarea. Hace una pausa corta y predecible.
        // Todo lo que está arriba se ejecuta cada 100 milisegundos.
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}