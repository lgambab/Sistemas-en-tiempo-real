// ===== INCLUDES Y CONFIGURACIÓN =====
#include "uart_commands.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "string.h"
#include "stdio.h"
#include "stdlib.h"
#include "rgb_led.h"

static const char *TAG = "UART_CMD";

// ===== CONFIGURACIÓN UART =====
#define UART_NUM UART_NUM_1  // Usar UART1 para no interferir con logs (UART0)
#define BUF_SIZE 1024
// Pines físicos para UART1 (conversor USB-Serial externo)
#define UART_TX_PIN 17       // Pin físico TX para UART1
#define UART_RX_PIN 16       // Pin físico RX para UART1

// Variables globales
static rgb_thresholds_t rgb_thresholds = {
    .r_min = 0.0, .r_max = 15.0,    // Rojo: 0-15°C
    .g_min = 10.0, .g_max = 30.0,   // Verde: 10-30°C
    .b_min = 40.0, .b_max = 50.0    // Azul: 40-50°C
};

// ===== FUNCIONES PRIVADAS =====
static void process_command(char* command)
{
    char* token = strtok(command, " ");
    
    if (token == NULL) return;
    
    // Comando: SET_R_THRESHOLD rmin rmax
    if (strcmp(token, "SET_R_THRESHOLD") == 0) {
        char* rmin_str = strtok(NULL, " ");
        char* rmax_str = strtok(NULL, " ");
        
        if (rmin_str && rmax_str) {
            float rmin = atof(rmin_str);
            float rmax = atof(rmax_str);
            
            if (rmin < rmax) {
                rgb_thresholds.r_min = rmin;
                rgb_thresholds.r_max = rmax;
                printf("Threshold Rojo actualizado: %.1f - %.1f°C\n", rmin, rmax);
            } else {
                printf("ERROR: rmin debe ser menor que rmax\n");
            }
        } else {
            printf("ERROR: Sintaxis: SET_R_THRESHOLD rmin rmax\n");
        }
    }
    // Comando: SET_G_THRESHOLD gmin gmax
    else if (strcmp(token, "SET_G_THRESHOLD") == 0) {
        char* gmin_str = strtok(NULL, " ");
        char* gmax_str = strtok(NULL, " ");
        
        if (gmin_str && gmax_str) {
            float gmin = atof(gmin_str);
            float gmax = atof(gmax_str);
            
            if (gmin < gmax) {
                rgb_thresholds.g_min = gmin;
                rgb_thresholds.g_max = gmax;
                printf("Threshold Verde actualizado: %.1f - %.1f°C\n", gmin, gmax);
            } else {
                printf("ERROR: gmin debe ser menor que gmax\n");
            }
        } else {
            printf("ERROR: Sintaxis: SET_G_THRESHOLD gmin gmax\n");
        }
    }
    // Comando: SET_B_THRESHOLD bmin bmax
    else if (strcmp(token, "SET_B_THRESHOLD") == 0) {
        char* bmin_str = strtok(NULL, " ");
        char* bmax_str = strtok(NULL, " ");
        
        if (bmin_str && bmax_str) {
            float bmin = atof(bmin_str);
            float bmax = atof(bmax_str);
            
            if (bmin < bmax) {
                rgb_thresholds.b_min = bmin;
                rgb_thresholds.b_max = bmax;
                printf("Threshold Azul actualizado: %.1f - %.1f°C\n", bmin, bmax);
            } else {
                printf("ERROR: bmin debe ser menor que bmax\n");
            }
        } else {
            printf("ERROR: Sintaxis: SET_B_THRESHOLD bmin bmax\n");
        }
    }
    // Comando: SHOW_THRESHOLDS
    else if (strcmp(token, "SHOW_THRESHOLDS") == 0) {
        printf("=== THRESHOLDS RGB ===\n");
        printf("Rojo:   %.1f - %.1f°C\n", rgb_thresholds.r_min, rgb_thresholds.r_max);
        printf("Verde:  %.1f - %.1f°C\n", rgb_thresholds.g_min, rgb_thresholds.g_max);
        printf("Azul:   %.1f - %.1f°C\n", rgb_thresholds.b_min, rgb_thresholds.b_max);
        printf("=====================\n");
    }
    // Comando: HELP
    else if (strcmp(token, "HELP") == 0) {
        printf("=== COMANDOS UART ===\n");
        printf("SET_R_THRESHOLD rmin rmax  - Configurar threshold rojo\n");
        printf("SET_G_THRESHOLD gmin gmax  - Configurar threshold verde\n");
        printf("SET_B_THRESHOLD bmin bmax  - Configurar threshold azul\n");
        printf("SHOW_THRESHOLDS           - Mostrar thresholds actuales\n");
        printf("HELP                      - Mostrar esta ayuda\n");
        printf("========================\n");
    }
    // Comando no reconocido
    else {
        printf("ERROR: Comando no reconocido: %s\n", token);
        printf("Escriba HELP para ver comandos disponibles\n");
    }
}

// ===== FUNCIONES PÚBLICAS =====
void uart_commands_init(void)
{
    ESP_LOGI(TAG, "Inicializando UART para comandos...");
    
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM, BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM, &uart_config));
    // Configurar pines físicos para UART1 (conversor USB-Serial externo)
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    
    ESP_LOGI(TAG, "UART inicializado correctamente");
    printf("\n=== SISTEMA DE COMANDOS UART ===\n");
    printf("Comandos disponibles:\n");
    printf("SET_R_THRESHOLD rmin rmax\n");
    printf("SET_G_THRESHOLD gmin gmax\n");
    printf("SET_B_THRESHOLD bmin bmax\n");
    printf("SHOW_THRESHOLDS\n");
    printf("HELP\n");
    printf("===============================\n");
}

void uart_commands_task(void *arg)
{
    uint8_t data[BUF_SIZE];
    char command_buffer[256];
    int command_pos = 0;
    
    ESP_LOGI(TAG, "Tarea de comandos UART iniciada");
    
    while (1) {
        int len = uart_read_bytes(UART_NUM, data, BUF_SIZE, 100 / portTICK_PERIOD_MS);
        
        if (len > 0) {
            for (int i = 0; i < len; i++) {
                char c = (char)data[i];
                
                if (c == '\n' || c == '\r') {
                    if (command_pos > 0) {
                        command_buffer[command_pos] = '\0';
                        process_command(command_buffer);
                        command_pos = 0;
                    }
                } else if (command_pos < sizeof(command_buffer) - 1) {
                    command_buffer[command_pos++] = c;
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

rgb_thresholds_t* get_rgb_thresholds(void)
{
    return &rgb_thresholds;
}

void update_rgb_by_temperature(float temperature)
{
    uint8_t red = 0, green = 0, blue = 0;
    
    // Verificar threshold rojo
    if (temperature >= rgb_thresholds.r_min && temperature <= rgb_thresholds.r_max) {
        red = 255;
    }
    
    // Verificar threshold verde
    if (temperature >= rgb_thresholds.g_min && temperature <= rgb_thresholds.g_max) {
        green = 255;
    }
    
    // Verificar threshold azul
    if (temperature >= rgb_thresholds.b_min && temperature <= rgb_thresholds.b_max) {
        blue = 255;
    }
    
    // Aplicar color al LED RGB
    rgb_led_set_color(red, green, blue);
}
