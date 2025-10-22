// ===== INCLUDES Y CONFIGURACIÓN =====
#include <stdio.h>
#include <stdarg.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#include "ntc_sensor.h"
#include "button_control.h"
#include "potentiometer.h"
#include "rgb_led.h"
// #include "uart_commands.h"  // Comentado: Listo para usar con conversor USB-Serial en UART1 (GPIO16/17)

static const char *TAG = "MAIN";

// ===== ESTRUCTURAS DE DATOS Y VARIABLES GLOBALES =====
typedef struct {
    uint8_t pot_percent;
    uint32_t pot_voltage_mv;
} pot_data_t;

static pot_data_t current_pot_data = {0};
static ntc_data_t current_ntc_data = {0};
static bool data_ready = false;

// ===== FUNCIÓN AUXILIAR PARA LOGS CONDICIONALES =====
static void conditional_log_info(const char *tag, const char *format, ...) {
    if (is_print_enabled()) {
        va_list args;
        va_start(args, format);
        esp_log_writev(ESP_LOG_INFO, tag, format, args);
        va_end(args);
    }
}

// ===== TAREAS DEL SISTEMA =====
void pot_reading_task(void *arg)
{
    pot_data_t pot_data;
    static uint8_t last_intensity = 0;
    static uint32_t last_log_time = 0;
    uint32_t current_time;
    
    ESP_LOGI(TAG, "Tarea de lectura del potenciómetro iniciada");
    
    while (1) {
        pot_data.pot_percent = pot_get_percent();
        pot_data.pot_voltage_mv = pot_get_voltage_mv();
        
        current_pot_data = pot_data;
        
        // Controlar intensidad del LED RGB con el potenciómetro (siempre en tiempo real)
        if (pot_data.pot_percent != last_intensity) {
            rgb_led_set_intensity(pot_data.pot_percent);
            last_intensity = pot_data.pot_percent;
        }
        
        // Solo imprimir cada 2 segundos
        current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (current_time - last_log_time >= 2000) {
            conditional_log_info(TAG, "Potenciómetro: %d%% (%lu mV) - LED RGB: %d%%", 
                               pot_data.pot_percent, pot_data.pot_voltage_mv, pot_data.pot_percent);
            last_log_time = current_time;
        }
        
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

void ntc_reading_task(void *arg)
{
    ntc_data_t ntc_data;
    
    ESP_LOGI(TAG, "Tarea de lectura del sensor NTC iniciada");
    
    while (1) {
        ntc_data = ntc_read_temperature();
        
        // Validar que los datos sean razonables
        if (ntc_data.raw_adc_value > 0 && ntc_data.raw_adc_value < 4096 && 
            ntc_data.temperature_c > -50.0 && ntc_data.temperature_c < 150.0 &&
            ntc_data.temperature_c != -999.0) {
            
            current_ntc_data = ntc_data;
            data_ready = true;
            
            // Actualizar LED RGB según temperatura y thresholds
            // update_rgb_by_temperature(ntc_data.temperature_c);  // Comentado: Listo para usar con conversor USB-Serial en UART1
            
            conditional_log_info(TAG, "Datos válidos: Temp=%.1f°C, ADC=%d, R=%.0fΩ", 
                     ntc_data.temperature_c, ntc_data.raw_adc_value, ntc_data.resistance);
        } else {
            conditional_log_info(TAG, "Datos inválidos: Temp=%.1f°C, ADC=%d, R=%.0fΩ", 
                     ntc_data.temperature_c, ntc_data.raw_adc_value, ntc_data.resistance);
        }
        
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}


void display_info_task(void *arg)
{
    ESP_LOGI(TAG, "Tarea de visualización iniciada");
    
    while (1) {
        // Solo mostrar datos si la impresión está habilitada
        if (is_print_enabled()) {
            printf("\n=== SISTEMA DE MONITOREO ===\n");
            printf("Potenciómetro: %d%% (%lu mV)\n", 
                   current_pot_data.pot_percent, current_pot_data.pot_voltage_mv);
            printf("LED RGB: %d%% intensidad\n", rgb_led_get_intensity());
            if (data_ready) {
                printf("Temperatura: %.1f°C\n", current_ntc_data.temperature_c);
                printf("Resistencia NTC: %.0f Ohms | ADC Raw: %d\n",
                       current_ntc_data.resistance, current_ntc_data.raw_adc_value);
            } else {
                printf("Esperando datos del sensor NTC...\n");
            }
            printf("Estado: Impresión HABILITADA (Botón GPIO14)\n");
            printf("==========================================\n\n");
        }
        // Cuando la impresión está deshabilitada, NO imprimir NADA
        // El monitor serial permanecerá completamente silencioso
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}


// ===== FUNCIÓN PRINCIPAL DEL SISTEMA =====
void app_main(void)
{
    ESP_LOGI(TAG, "=== INICIANDO SISTEMA RTOS - SENSOR NTC ===");
    ESP_LOGI(TAG, "Inicializando componentes de hardware...");
    
    // ===== INICIALIZACIÓN DE HARDWARE =====
    ntc_sensor_init();
    button_control_init();
    pot_init();
    rgb_led_init();
    // uart_commands_init();  // Comentado: Listo para usar con conversor USB-Serial en UART1 (GPIO16/17)
    
    ESP_LOGI(TAG, "Hardware inicializado correctamente");
    
    
    
    // ===== CREACIÓN DE TAREAS DEL SISTEMA =====
    ESP_LOGI(TAG, "Creando tareas del sistema...");
    
    if (xTaskCreate(pot_reading_task, "pot_reading_task", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Error creando tarea de lectura del potenciómetro");
        return;
    }
    
    if (xTaskCreate(ntc_reading_task, "ntc_reading_task", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Error creando tarea de lectura del sensor NTC");
        return;
    }
    
    
    if (xTaskCreate(display_info_task, "display_info_task", 4096, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Error creando tarea de visualización");
        return;
    }
    
    if (xTaskCreate(button_task, "button_task", 4096, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Error creando tarea de control de botón");
        return;
    }
    
    // Comentado: Listo para usar con conversor USB-Serial en UART1 (GPIO16/17)
    // if (xTaskCreate(uart_commands_task, "uart_commands_task", 4096, NULL, 3, NULL) != pdPASS) {
    //     ESP_LOGE(TAG, "Error creando tarea de comandos UART");
    //     return;
    // }
    
    // ===== SISTEMA INICIADO EXITOSAMENTE =====
    ESP_LOGI(TAG, "=== SISTEMA RTOS INICIADO EXITOSAMENTE ===");
    ESP_LOGI(TAG, "Configuración del hardware:");
    ESP_LOGI(TAG, "  - Potenciómetro: ADC1 CH6 (GPIO34)");
    ESP_LOGI(TAG, "  - Sensor NTC: ADC2 CH9 (GPIO26)");
    ESP_LOGI(TAG, "  - Botón de control: GPIO14");
    ESP_LOGI(TAG, "  - LED RGB: R=GPIO13, G=GPIO12, B=GPIO25");
    // ESP_LOGI(TAG, "  - UART: USB-Serial (UART1) - GPIO16/17 - 115200 baud");  // Comentado: Listo para usar con conversor USB-Serial
    ESP_LOGI(TAG, "Frecuencias de operación:");
    ESP_LOGI(TAG, "  - Lectura potenciómetro: 4 veces/segundo");
    ESP_LOGI(TAG, "  - Lectura sensor NTC: cada 2 segundos");
    ESP_LOGI(TAG, "  - Monitor serie: cada 1 segundo");
    ESP_LOGI(TAG, "  - Control de botón: cada 10ms");
    // ESP_LOGI(TAG, "  - Comandos UART: cada 10ms");  // Comentado: Listo para usar con conversor USB-Serial en UART1
    ESP_LOGI(TAG, "Controles:");
    ESP_LOGI(TAG, "  - Pulsación corta: Alternar impresión ON/OFF");
    ESP_LOGI(TAG, "  - Pulsación larga: Evento especial");
    ESP_LOGI(TAG, "  - Potenciómetro: Controla intensidad LED RGB (0-100%)");
    // ESP_LOGI(TAG, "  - UART: Comandos para configurar thresholds RGB");  // Comentado: Listo para usar con conversor USB-Serial en UART1
    ESP_LOGI(TAG, "=== SISTEMA EN FUNCIONAMIENTO ===");
}