#include <nvs_flash.h>
#include "wifi_app.h"
#include "web_server.h"
#include "gpio_driver.h"
#include "ntc_sensor.h"

void app_main(void) {
    // 1. Inicializar NVS (Necesario para WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Inicializar Hardware
    gpio_init_leds();
    ntc_sensor_init();
    ntc_start_reading_task(); // Iniciar tarea de lectura periódica

    // 3. Inicializar WiFi (SoftAP)
    // (Implementa wifi_init_softap en wifi_app.c usando ejemplos standard de ESP-IDF)
    wifi_init_softap(); 

    // 4. Iniciar Servidor Web
    start_webserver();
}