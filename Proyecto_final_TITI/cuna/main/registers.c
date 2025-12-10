/**
 * @file registers.c
 * @brief Implementación del sistema de programación de eventos (scheduler)
 * @author Jair Hernan Telpis Cuaran, Luis Fernando Gamba Bedoya
 * @date 2025
 * 
 * @details
 * Este módulo implementa un scheduler de eventos programados para activación automática
 * del ventilador basado en hora y días de la semana.
 * 
 * **Características:**
 * - Hasta 10 registros programables
 * - Configuración por hora:minuto y días (L,M,X,J,V,S,D)
 * - Persistencia en NVS (flash) - sobrevive a reinicios
 * - Evaluación cada 10 segundos por tarea RTOS dedicada
 * - API REST completa (POST/GET/DELETE)
 * 
 * **Arquitectura:**
 * @code
 *   [Web UI] --POST--> /api/register --> save_register_to_nvs()
 *       |                                        |
 *       |                                        v
 *       |                                   [NVS Flash]
 *       |                                        ^
 *       v                                        |
 *   [registers_scheduler_task] ---cada 10s---> load_register_from_nvs()
 *       |                                        |
 *       v                                        v
 *   register_is_active_now() <--- compara hora/día actual
 *       |
 *       v
 *   fan_control_on_register_tick(any_match) --> Enciende/apaga ventilador
 * @endcode
 * 
 * **Formato JSON en NVS:**
 * @code
 * {
 *   "hour": 14,
 *   "minute": 30,
 *   "days": ["L", "X", "V"]  // Lunes, Miércoles, Viernes
 * }
 * @endcode
 * 
 * **Mapeo de días:**
 * - L = Lunes (tm_wday = 1)
 * - M = Martes (tm_wday = 2)
 * - X = Miércoles (tm_wday = 3)
 * - J = Jueves (tm_wday = 4)
 * - V = Viernes (tm_wday = 5)
 * - S = Sábado (tm_wday = 6)
 * - D = Domingo (tm_wday = 0)
 * 
 * @see registers.h Para la API pública y estructura reg_t
 * @see fan_control.c Para activación del ventilador
 */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "sdkconfig.h"
#include "registers.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "cJSON.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "fan_control.h"

// Fix for VS Code IntelliSense
#if defined(__INTELLISENSE__)
#ifndef CONFIG_LOG_MAXIMUM_LEVEL
#define CONFIG_LOG_MAXIMUM_LEVEL 5
#endif
#endif

/** @brief Tag para logging ESP-IDF */
static const char *TAG = "REGISTERS";

/**
 * @brief Inicializa el subsistema NVS (Non-Volatile Storage)
 * 
 * @details
 * Inicializa la partición NVS en flash para almacenamiento persistente.
 * Si la partición está corrupta o es versión antigua, la borra y reinicializa.
 * 
 * **Manejo de errores:**
 * - ESP_ERR_NVS_NO_FREE_PAGES: Partición llena → borra y reinicia
 * - ESP_ERR_NVS_NEW_VERSION_FOUND: Versión incompatible → borra y reinicia
 * - Otros errores: ESP_ERROR_CHECK causa panic
 * 
 * @warning Llamar antes de cualquier operación NVS
 * @note Seguro llamar múltiples veces (NVS ignora reinicializaciones)
 */
void init_nvs()
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

/**
 * @brief Guarda un registro en NVS como JSON
 * 
 * @details
 * Serializa la estructura reg_t a JSON y la guarda en NVS con clave "reg_<id>".
 * 
 * **Proceso:**
 * 1. Crea objeto JSON con hour, minute, days
 * 2. Serializa a string con cJSON_Print()
 * 3. Guarda en NVS con nvs_set_str()
 * 4. Commit para escribir a flash
 * 
 * **Ejemplo de JSON guardado:**
 * @code
 * {"hour":14,"minute":30,"days":["L","X","V"]}
 * @endcode
 * 
 * @param[in] id ID del registro (1-10)
 * @param[in] reg Puntero a estructura reg_t a guardar
 * 
 * @return esp_err_t
 * @retval ESP_OK Guardado exitoso
 * @retval ESP_FAIL Error de NVS
 * 
 * @warning Sobrescribe registro existente con mismo ID
 * @note Máximo 10 registros soportados (IDs 1-10)
 */
esp_err_t save_register_to_nvs(int id, reg_t *reg)
{
    nvs_handle_t nvs;
    char key[16];
    sprintf(key, "reg_%d", id);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "hour",   reg->hour);
    cJSON_AddNumberToObject(root, "minute", reg->minute);

    cJSON *days = cJSON_CreateArray();
    for (int i = 0; i < reg->day_count; i++) {
        cJSON_AddItemToArray(days, cJSON_CreateString(reg->days[i]));
    }
    cJSON_AddItemToObject(root, "days", days);

    char *json_str = cJSON_Print(root);

    ESP_LOGI(TAG, "save_register_to_nvs: id=%d json=%s", id, json_str);

    ESP_ERROR_CHECK(nvs_open("storage", NVS_READWRITE, &nvs));
    esp_err_t err = nvs_set_str(nvs, key, json_str);
    nvs_commit(nvs);
    nvs_close(nvs);

    free(json_str);
    cJSON_Delete(root);

    return err;
}

/**
 * @brief Carga un registro desde NVS como string JSON
 * 
 * @details
 * Lee el string JSON almacenado en NVS para el ID especificado.
 * 
 * **Proceso:**
 * 1. Abre namespace "storage" en modo lectura
 * 2. Verifica tamaño requerido con nvs_get_str(NULL)
 * 3. Lee string JSON en buffer proporcionado
 * 4. Cierra handle NVS
 * 
 * @param[in] id ID del registro (1-10)
 * @param[out] buffer Buffer donde copiar el JSON
 * @param[in] buffer_len Tamaño del buffer
 * 
 * @return esp_err_t
 * @retval ESP_OK Lectura exitosa
 * @retval ESP_ERR_NVS_NOT_FOUND No existe registro con ese ID
 * @retval ESP_ERR_NVS_INVALID_LENGTH Buffer muy pequeño
 * 
 * @note No parsea el JSON - solo retorna string crudo
 * @see load_register_struct_from_nvs() Para obtener estructura reg_t directamente
 */
esp_err_t load_register_from_nvs(int id, char *buffer, size_t buffer_len)
{
    nvs_handle_t nvs;
    char key[16];
    sprintf(key, "reg_%d", id);

    ESP_ERROR_CHECK(nvs_open("storage", NVS_READWRITE, &nvs));

    size_t required_size = 0;
    esp_err_t err = nvs_get_str(nvs, key, NULL, &required_size);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(nvs);
        return ESP_ERR_NOT_FOUND;
    }

    if (required_size > buffer_len) {
        nvs_close(nvs);
        return ESP_ERR_NVS_INVALID_LENGTH;
    }

    err = nvs_get_str(nvs, key, buffer, &required_size);
    nvs_close(nvs);
    return err;
}

/**
 * @brief Elimina un registro de NVS
 * 
 * @details
 * Borra la clave "reg_<id>" del namespace "storage".
 * 
 * @param[in] id ID del registro a eliminar (1-10)
 * 
 * @note No retorna error si el registro no existía (nvs_erase_key silencioso)
 * @warning Requiere nvs_commit() para persistir (ya incluido)
 */
void delete_register_from_nvs(int id)
{
    nvs_handle_t nvs;
    char key[16];
    sprintf(key, "reg_%d", id);

    ESP_ERROR_CHECK(nvs_open("storage", NVS_READWRITE, &nvs));
    nvs_erase_key(nvs, key);
    nvs_commit(nvs);
    nvs_close(nvs);
}

/**
 * @brief Helper: Carga registro desde NVS y parsea a estructura reg_t
 * 
 * @details
 * Combina load_register_from_nvs() + parsing JSON.
 * 
 * **Proceso:**
 * 1. Lee string JSON de NVS
 * 2. Parsea con cJSON_Parse()
 * 3. Extrae campos: hour, minute, days
 * 4. Valida tipos JSON (number, array)
 * 5. Copia a estructura reg_t
 * 
 * @param[in] id ID del registro (1-10)
 * @param[out] out Puntero a reg_t donde copiar datos
 * 
 * @return esp_err_t
 * @retval ESP_OK Carga y parsing exitoso
 * @retval ESP_ERR_NVS_NOT_FOUND No existe registro
 * @retval ESP_FAIL JSON inválido o campos faltantes
 * 
 * @note Función estática (solo visible en este módulo)
 * @note Limita días a máximo 7
 */
static esp_err_t load_register_struct_from_nvs(int id, reg_t *out)
{
    char buf[512];
    esp_err_t err = load_register_from_nvs(id, buf, sizeof(buf));
    if (err != ESP_OK) {
        return err;
    }

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        return ESP_FAIL;
    }

    cJSON *hour_json = cJSON_GetObjectItem(root, "hour");
    cJSON *min_json  = cJSON_GetObjectItem(root, "minute");
    cJSON *days_json = cJSON_GetObjectItem(root, "days");

    if (!cJSON_IsNumber(hour_json) || !cJSON_IsNumber(min_json) || !cJSON_IsArray(days_json)) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    memset(out, 0, sizeof(*out));
    out->hour      = hour_json->valueint;
    out->minute    = min_json->valueint;
    out->day_count = cJSON_GetArraySize(days_json);
    if (out->day_count > 7) {
        out->day_count = 7;
    }

    for (int i = 0; i < out->day_count; i++) {
        cJSON *item = cJSON_GetArrayItem(days_json, i);
        if (cJSON_IsString(item) && item->valuestring) {
            strncpy(out->days[i], item->valuestring, sizeof(out->days[i]) - 1);
            out->days[i][sizeof(out->days[i]) - 1] = '\0';
        } else {
            out->days[i][0] = '\0';
        }
    }

    cJSON_Delete(root);
    return ESP_OK;
}

/**
 * @brief Determina si un registro debe activarse en este momento
 * 
 * @details
 * Compara hora actual del sistema con el registro programado.
 * 
 * **Algoritmo:**
 * 1. Obtiene hora del sistema (time() + localtime_r())
 * 2. Compara hora:minuto exacto
 * 3. Compara día de la semana (tm_wday)
 * 4. Retorna true si hay coincidencia
 * 
 * **Mapeo de días:**
 * - tm_wday: 0=Domingo, 1=Lunes, 2=Martes, 3=Miércoles, 4=Jueves, 5=Viernes, 6=Sábado
 * - Registro: "L","M","X","J","V","S","D"
 * 
 * **Ventana de activación:**
 * Solo activo durante el minuto exacto (14:30:00 a 14:30:59).
 * Scheduler debe llamar esta función con frecuencia ≤ 60s.
 * 
 * @param[in] reg Puntero a registro a evaluar
 * 
 * @return bool
 * @retval true Registro coincide con hora/día actual
 * @retval false No coincide o día no incluido
 * 
 * @note Función estática (solo visible en este módulo)
 * @warning Requiere que el sistema tenga hora sincronizada (SNTP)
 */
static bool register_is_active_now(const reg_t *reg)
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    int cur_hour = timeinfo.tm_hour;
    int cur_min  = timeinfo.tm_min;
    int cur_wday = timeinfo.tm_wday;  // 0=domingo, 1=lunes, ... 6=sábado

    if (reg->hour != cur_hour || reg->minute != cur_min) {
        return false;
    }

    for (int i = 0; i < reg->day_count; i++) {
        const char *d = reg->days[i];

        if ((cur_wday == 1 && strcmp(d, "L") == 0) ||   // lunes
            (cur_wday == 2 && strcmp(d, "M") == 0) ||   // martes
            (cur_wday == 3 && strcmp(d, "X") == 0) ||   // miércoles
            (cur_wday == 4 && strcmp(d, "J") == 0) ||   // jueves
            (cur_wday == 5 && strcmp(d, "V") == 0) ||   // viernes
            (cur_wday == 6 && strcmp(d, "S") == 0) ||   // sábado
            (cur_wday == 0 && strcmp(d, "D") == 0)) {   // domingo
            return true;
        }
    }

    return false;
}

// -------------------- HTTP: POST /api/register --------------------
esp_err_t api_post_register(httpd_req_t *req)
{
    int len = req->content_len;
    if (len <= 0 || len > 4096) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid length");
        return ESP_FAIL;
    }

    char *buf = malloc(len + 1);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no mem");
        return ESP_FAIL;
    }

    int ret = httpd_req_recv(req, buf, len);
    if (ret <= 0) {
        free(buf);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv failed");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);

    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
        return ESP_FAIL;
    }

    cJSON *id_json   = cJSON_GetObjectItem(root, "register");
    cJSON *hour_json = cJSON_GetObjectItem(root, "hour");
    cJSON *min_json  = cJSON_GetObjectItem(root, "minute");
    cJSON *days      = cJSON_GetObjectItem(root, "days");

    if (!cJSON_IsNumber(id_json) || !cJSON_IsNumber(hour_json) ||
        !cJSON_IsNumber(min_json) || !cJSON_IsArray(days)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing fields");
        return ESP_FAIL;
    }

    int id     = id_json->valueint;
    int hour   = hour_json->valueint;
    int minute = min_json->valueint;

    int day_count = cJSON_GetArraySize(days);
    if (day_count > 7) day_count = 7;

    reg_t reg = {0};
    reg.hour      = hour;
    reg.minute    = minute;
    reg.day_count = day_count;

    for (int i = 0; i < day_count; i++) {
        cJSON *item = cJSON_GetArrayItem(days, i);
        if (cJSON_IsString(item) && item->valuestring) {
            strncpy(reg.days[i], item->valuestring, sizeof(reg.days[i]) - 1);
            reg.days[i][sizeof(reg.days[i]) - 1] = '\0';
        } else {
            reg.days[i][0] = '\0';
        }
    }

    esp_err_t err = save_register_to_nvs(id, &reg);
    cJSON_Delete(root);

    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "nvs error");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "api_post_register: saved id=%d hour=%d minute=%d days=%d",
             id, hour, minute, day_count);

    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

// -------------------- HTTP: GET /api/registers --------------------
esp_err_t api_get_registers(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    char smallbuf[256];

    for (int i = 1; i <= 10; i++) {
        char key[8];
        snprintf(key, sizeof(key), "%d", i);

        esp_err_t r = load_register_from_nvs(i, smallbuf, sizeof(smallbuf));
        if (r == ESP_OK) {
            cJSON *reg_json = cJSON_Parse(smallbuf);
            if (reg_json) {
                cJSON_AddItemToObject(root, key, reg_json);
                continue;
            }
        } else if (r == ESP_ERR_NVS_INVALID_LENGTH) {
            nvs_handle_t nvs;
            char nkey[16];
            snprintf(nkey, sizeof(nkey), "reg_%d", i);
            if (nvs_open("storage", NVS_READWRITE, &nvs) == ESP_OK) {
                size_t required = 0;
                if (nvs_get_str(nvs, nkey, NULL, &required) == ESP_OK && required > 0) {
                    char *dyn = malloc(required);
                    if (dyn) {
                        if (nvs_get_str(nvs, nkey, dyn, &required) == ESP_OK) {
                            cJSON *reg_json = cJSON_Parse(dyn);
                            if (reg_json) {
                                cJSON_AddItemToObject(root, key, reg_json);
                            }
                        }
                        free(dyn);
                    }
                }
                nvs_close(nvs);
                continue;
            }
        }

        cJSON_AddNullToObject(root, key);
    }

    char *out = cJSON_PrintUnformatted(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, out, strlen(out));

    free(out);
    cJSON_Delete(root);
    return ESP_OK;

}


// Removed redundant local wrapper: http_server.c provides its own handler.

// -------------------- HTTP: DELETE /api/register/{id} --------------------
esp_err_t api_delete_register(httpd_req_t *req)
{
    const char *uri = req->uri; // e.g. "/api/register/3"
    int id = 0;

    if (uri) {
        const char *last = strrchr(uri, '/');
        if (last && *(last + 1) != '\0') {
            id = atoi(last + 1);
        }
    }

    if (id <= 0) {
        char query[16] = {0};
        if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
            id = atoi(query);
        }
    }

    if (id <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid id");
        return ESP_FAIL;
    }

    delete_register_from_nvs(id);
    ESP_LOGI(TAG, "api_delete_register: deleted id=%d", id);

    httpd_resp_sendstr(req, "Deleted");
    return ESP_OK;
}

/**
 * @brief Tarea RTOS que evalúa registros programados periódicamente
 * 
 * @details
 * Scheduler que evalúa los 10 registros cada 10 segundos.
 * 
 * **Ciclo de ejecución:**
 * 1. Itera sobre IDs 1-10
 * 2. Carga cada registro desde NVS
 * 3. Evalúa con register_is_active_now()
 * 4. Si alguno coincide: any_match_now = true
 * 5. Llama fan_control_on_register_tick(any_match_now)
 * 6. Espera 10 segundos (vTaskDelay)
 * 
 * **Optimización:**
 * Break al primer match (no evalúa registros restantes).
 * 
 * **Parámetros de tarea:**
 * - Nombre: "registers_scheduler"
 * - Stack: 4096 bytes
 * - Prioridad: 5 (media-alta)
 * - Periodo: 10000ms
 * 
 * @param[in] arg Parámetro de tarea (no usado)
 * 
 * @note Tarea infinita (nunca termina)
 * @warning Periodo de 10s significa resolución mínima de activación de 10s
 * @see fan_control_on_register_tick() Para activación del ventilador
 */
static void registers_scheduler_task(void *arg)
{
    ESP_LOGI(TAG, "registers_scheduler_task: started");

    while (1) {
        bool any_match_now = false;

        for (int i = 1; i <= 10; i++) {
            reg_t reg;
            if (load_register_struct_from_nvs(i, &reg) == ESP_OK) {
                if (register_is_active_now(&reg)) {
                    any_match_now = true;
                    break;
                }
            }
        }

        fan_control_on_register_tick(any_match_now);

        vTaskDelay(pdMS_TO_TICKS(10000)); // cada 10 s
    }
}

// -------------------- REGISTRO DE ENDPOINTS --------------------
void register_registers_endpoints(httpd_handle_t server)
{
    ESP_LOGI(TAG, "register_registers_endpoints: registering /api/register endpoints");

    httpd_uri_t post_register_uri = {
        .uri     = "/api/register",
        .method  = HTTP_POST,
        .handler = api_post_register
    };

    httpd_uri_t get_registers_uri = {
        .uri     = "/api/registers",
        .method  = HTTP_GET,
        .handler = api_get_registers
    };

    httpd_uri_t delete_register_uri = {
        .uri     = "/api/register/*",
        .method  = HTTP_DELETE,
        .handler = api_delete_register
    };

    httpd_register_uri_handler(server, &post_register_uri);
    httpd_register_uri_handler(server, &get_registers_uri);
    httpd_register_uri_handler(server, &delete_register_uri);

    static bool s_scheduler_started = false;
    if (!s_scheduler_started) {
        if (xTaskCreate(
                registers_scheduler_task,
                "registers_scheduler",
                4096,
                NULL,
                5,
                NULL
            ) == pdPASS)
        {
            s_scheduler_started = true;
            ESP_LOGI(TAG, "registers_scheduler_task created");
        } else {
            ESP_LOGE(TAG, "Failed to create registers_scheduler_task");
        }
    }
}
