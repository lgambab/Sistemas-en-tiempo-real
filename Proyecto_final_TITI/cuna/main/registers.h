// ============================================================
// registers.h — Gestión de registros (NVS + JSON + HTTP API)
// ============================================================

#pragma once
#include "esp_err.h"
#include "esp_http_server.h"

// Estructura de un registro
typedef struct {
    int hour;
    int minute;
    char days[7][12];   // Lunes, Martes...
    int day_count;
} register_t;

// Inicialización
void init_nvs();

// Guardar un registro
esp_err_t save_register_to_nvs(int id, register_t *reg);

// Cargar un registro (devuelve JSON en buffer)
esp_err_t load_register_from_nvs(int id, char *buffer, size_t buffer_len);

// Borrar un registro
void delete_register_from_nvs(int id);

// Handlers HTTP
esp_err_t api_post_register(httpd_req_t *req);
esp_err_t api_get_registers(httpd_req_t *req);
esp_err_t api_delete_register(httpd_req_t *req);

// Función para registrar las rutas del servidor
void register_registers_endpoints(httpd_handle_t server);
