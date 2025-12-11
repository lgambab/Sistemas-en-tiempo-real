// auth_display.c
#include "auth_display.h"
#include "peripherals.h"
#include "sensor_task.h"
#include "fan_control.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include <string.h>
#include <time.h>
#include <sys/time.h>

static const char *TAG = "auth_display";

#define MAX_PASSWORD_LEN 8
#define DEFAULT_PASSWORD "1234"
#define INFO_LED_GPIO GPIO_NUM_22  // LED indicador de información activa

static char stored_password[MAX_PASSWORD_LEN + 1] = DEFAULT_PASSWORD;
static char input_buffer[MAX_PASSWORD_LEN + 1] = {0};
static int input_index = 0;
static bool authenticated = false;

// Declaración forward
static void show_info_screen(void);

// Tarea de actualización periódica
static TaskHandle_t refresh_task_handle = NULL;

static void info_refresh_task(void* arg) {
    ESP_LOGI(TAG, "Info refresh task started");
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));  // Esperar 1 segundo
        
        // Solo refrescar si estamos autenticados
        if (authenticated) {
            show_info_screen();
        }
    }
}

/**
 * @brief Carga la contraseña desde NVS
 * @return esp_err_t ESP_OK si se cargó, ESP_ERR_NVS_NOT_FOUND si no existe
 */
static esp_err_t load_password_from_nvs(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &nvs_handle);
    
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS for reading password");
        return err;
    }
    
    size_t required_size = MAX_PASSWORD_LEN + 1;
    err = nvs_get_str(nvs_handle, "oled_password", stored_password, &required_size);
    
    nvs_close(nvs_handle);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Password loaded from NVS");
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved password, using default: %s", DEFAULT_PASSWORD);
    } else {
        ESP_LOGW(TAG, "Error reading password from NVS: %s", esp_err_to_name(err));
    }
    
    return err;
}

/**
 * @brief Guarda la contraseña en NVS
 * @return esp_err_t ESP_OK si se guardó correctamente
 */
static esp_err_t save_password_to_nvs(const char* password) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing password");
        return err;
    }
    
    err = nvs_set_str(nvs_handle, "oled_password", password);
    
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Password saved to NVS");
        } else {
            ESP_LOGE(TAG, "Failed to commit password to NVS");
        }
    } else {
        ESP_LOGE(TAG, "Failed to set password in NVS");
    }
    
    nvs_close(nvs_handle);
    return err;
}

esp_err_t auth_display_init(void) {
    // Configurar GPIO 22 como salida para el LED
    gpio_reset_pin(INFO_LED_GPIO);
    gpio_set_direction(INFO_LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(INFO_LED_GPIO, 0);  // LED apagado inicialmente
    
    // Intentar cargar contraseña desde NVS
    esp_err_t err = load_password_from_nvs();
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Using default password due to NVS error");
    }
    
    ESP_LOGI(TAG, "Authentication system initialized");
    ESP_LOGI(TAG, "Current password length: %d characters", strlen(stored_password));
    ESP_LOGI(TAG, "Info LED configured on GPIO %d", INFO_LED_GPIO);
    
    // Mostrar prompt inicial
    peripherals_oled_show_text("Password:");
    
    // Crear tarea de refresco
    xTaskCreate(info_refresh_task, "info_refresh", 3072, NULL, 
                configMAX_PRIORITIES - 4, &refresh_task_handle);
    
    return ESP_OK;
}

esp_err_t auth_display_set_password(const char* password) {
    if (!password || strlen(password) > MAX_PASSWORD_LEN) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Actualizar en RAM
    strncpy(stored_password, password, MAX_PASSWORD_LEN);
    stored_password[MAX_PASSWORD_LEN] = '\0';
    
    // Guardar en NVS para persistencia
    esp_err_t err = save_password_to_nvs(password);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Password updated in RAM but failed to save to NVS");
        return err;
    }
    
    ESP_LOGI(TAG, "Password updated and saved to NVS");
    return ESP_OK;
}

static void show_info_screen(void) {
    // Leer desde tarea de sensores (thread-safe)
    sensor_data_t sensor_data;
    esp_err_t ret = sensor_task_get_data(&sensor_data);
    
    char line[32];
    
    // Limpiar pantalla
    peripherals_oled_clear();
    
    if (ret == ESP_OK && sensor_data.time_valid) {
        // Hora válida - Línea 1 (y=0)
        snprintf(line, sizeof(line), "Hora:");
        peripherals_oled_draw_text(0, 0, line);
        
        // Hora valor - Línea 2 (y=12)
        snprintf(line, sizeof(line), "%02d:%02d:%02d",
                 sensor_data.time_info.tm_hour, 
                 sensor_data.time_info.tm_min, 
                 sensor_data.time_info.tm_sec);
        peripherals_oled_draw_text(0, 12, line);
        
        // Temperatura - Línea 4 (y=32)
        float temp = sensor_data.temperature_valid ? sensor_data.temperature_celsius : 0.0f;
        snprintf(line, sizeof(line), "Temp: %.1fC", temp);
        peripherals_oled_draw_text(0, 32, line);
        
        // Ventilador - Línea 5 (y=44)
        uint8_t fan_speed = fan_control_get_speed();
        snprintf(line, sizeof(line), "Fan: %d%%", fan_speed);
        peripherals_oled_draw_text(0, 44, line);
        
        ESP_LOGI(TAG, "Info displayed: Time %02d:%02d:%02d, Temp %.1fC, Fan %d%%",
                 sensor_data.time_info.tm_hour, sensor_data.time_info.tm_min, 
                 sensor_data.time_info.tm_sec, temp, fan_speed);
    } else {
        // Datos no disponibles
        peripherals_oled_draw_text(0, 0, "Hora:");
        peripherals_oled_draw_text(0, 12, "--:--:--");
        peripherals_oled_draw_text(0, 32, "Temp: --C");
        
        uint8_t fan_speed = fan_control_get_speed();
        snprintf(line, sizeof(line), "Fan: %d%%", fan_speed);
        peripherals_oled_draw_text(0, 44, line);
        
        ESP_LOGI(TAG, "Sensor data not available");
    }
    
    // Mensaje de salida - Línea 7 (y=52)
    peripherals_oled_draw_text(0, 52, "# = salir");
}

static void reset_input(void) {
    memset(input_buffer, 0, sizeof(input_buffer));
    input_index = 0;
}

bool auth_display_is_active(void) {
    return authenticated;
}

void auth_display_process_key(char key) {
    if (authenticated) {
        // En modo autenticado, # sale
        if (key == '#') {
            authenticated = false;
            reset_input();
            gpio_set_level(INFO_LED_GPIO, 0);  // Apagar LED al salir
            peripherals_oled_show_text("Logged out\n\nPassword:");
            ESP_LOGI(TAG, "User logged out - LED OFF");
            return;
        }
        // Cualquier otra tecla: actualizar pantalla de info
        show_info_screen();
        return;
    }
    
    // Modo no autenticado: ingresar contraseña
    if (key == '#') {
        // # = Enter/Confirmar
        if (strcmp(input_buffer, stored_password) == 0) {
            // Contraseña correcta
            authenticated = true;
            gpio_set_level(INFO_LED_GPIO, 1);  // Encender LED al autenticar
            ESP_LOGI(TAG, "Authentication successful - LED ON");
            peripherals_oled_show_text("Access OK!\n\nLoading...");
            vTaskDelay(pdMS_TO_TICKS(1000));
            show_info_screen();
        } else {
            // Contraseña incorrecta
            ESP_LOGW(TAG, "Authentication failed: '%s' != '%s'", input_buffer, stored_password);
            peripherals_oled_show_text("Wrong pass!\n\nTry again:");
            vTaskDelay(pdMS_TO_TICKS(1500));
            peripherals_oled_show_text("Password:");
        }
        reset_input();
        return;
    }
    
    if (key == '*') {
        // * = Borrar último carácter
        if (input_index > 0) {
            input_index--;
            input_buffer[input_index] = '\0';
        }
    } else if (input_index < MAX_PASSWORD_LEN) {
        // Agregar carácter
        input_buffer[input_index++] = key;
        input_buffer[input_index] = '\0';
    }
    
    // Mostrar asteriscos para ocultar password
    char display[32] = "Password:\n";
    for (int i = 0; i < input_index; i++) {
        strcat(display, "*");
    }
    peripherals_oled_show_text(display);
    
    ESP_LOGI(TAG, "Input: %d chars", input_index);
}
