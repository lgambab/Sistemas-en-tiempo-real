#pragma once

// Definimos las credenciales aquí o podrías usar menuconfig
#define ESP_WIFI_SSID      "ESP32_Server"
#define ESP_WIFI_PASS      "12345678"
#define ESP_WIFI_CHANNEL   1
#define MAX_STA_CONN       4

// Inicializa el SoftAP
void wifi_init_softap(void);