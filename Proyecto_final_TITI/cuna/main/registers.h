/**
 * @file registers.h
 * @brief API REST para gestión de registros programados con persistencia NVS
 * @author Jair Hernan Telpis Cuaran, Luis Fernando Gamba Bedoya
 * @date 2025
 * @version 1.0.0
 * 
 * @details Sistema completo de registros programados que permite:
 * - Crear, leer y eliminar registros mediante endpoints HTTP REST
 * - Almacenar registros en NVS (Non-Volatile Storage) con formato JSON
 * - Comparar hora actual con registros para activar automáticamente el ventilador
 * - Sincronización mediante SNTP para precisión horaria
 * 
 * Cada registro define una hora, minuto y días de la semana para ejecutar
 * una acción (ej: encender ventilador). Los registros se almacenan en NVS
 * con claves "reg01" hasta "reg10" y persisten tras reinicios.
 * 
 * **Endpoints HTTP:**
 * - POST /api/register - Crear/actualizar registro
 * - GET  /api/register - Listar todos los registros
 * - DELETE /api/register?id=X - Eliminar registro
 * 
 * Universidad Nacional de Colombia - Curso RTOS
 */

#pragma once
#include "esp_err.h"
#include "esp_http_server.h"

/**
 * @struct reg_t
 * @brief Estructura de un registro programado
 * 
 * Define una acción programada con hora, minuto y días de la semana.
 */
typedef struct {
    int hour;               /**< Hora de ejecución (0-23) */
    int minute;             /**< Minuto de ejecución (0-59) */
    char days[7][12];       /**< Array de días: "Lunes", "Martes", etc. */
    int day_count;          /**< Cantidad de días válidos en el array */
} reg_t;

/**
 * @brief Inicializar sistema NVS para almacenamiento persistente
 * 
 * Debe llamarse una vez durante el arranque del sistema antes de
 * usar cualquier función de registros.
 * 
 * @note Inicializa la partición NVS requerida para guardar registros
 */
void init_nvs();

/**
 * @brief Guardar registro en NVS con formato JSON
 * 
 * Serializa el registro a JSON y lo almacena en NVS con clave "regXX".
 * Si ya existe un registro con ese ID, lo sobrescribe.
 * 
 * @param id Identificador del registro (1-10)
 * @param reg Puntero a estructura reg_t con los datos del registro
 * @return ESP_OK si se guardó correctamente
 * @return ESP_ERR_INVALID_ARG si id está fuera de rango o reg es NULL
 * @return ESP_FAIL si hubo error al escribir en NVS
 */
esp_err_t save_register_to_nvs(int id, reg_t *reg);

/**
 * @brief Cargar registro desde NVS
 * 
 * Lee el registro con el ID especificado desde NVS y lo retorna
 * como string JSON en el buffer proporcionado.
 * 
 * @param id Identificador del registro (1-10)
 * @param[out] buffer Buffer donde se escribirá el JSON
 * @param buffer_len Tamaño del buffer
 * @return ESP_OK si el registro existe y se cargó
 * @return ESP_ERR_NOT_FOUND si no existe registro con ese ID
 * @return ESP_ERR_INVALID_SIZE si el buffer es muy pequeño
 */
esp_err_t load_register_from_nvs(int id, char *buffer, size_t buffer_len);

/**
 * @brief Eliminar registro de NVS
 * 
 * Borra permanentemente el registro con el ID especificado de la
 * memoria no volátil.
 * 
 * @param id Identificador del registro a eliminar (1-10)
 */
void delete_register_from_nvs(int id);

/**
 * @brief Handler HTTP POST para crear/actualizar registro
 * 
 * Endpoint: POST /api/register
 * Body JSON: {"id":1, "hour":14, "minute":30, "days":["Lunes","Viernes"]}
 * 
 * @param req Request HTTP del servidor
 * @return ESP_OK si se procesó correctamente
 */
esp_err_t api_post_register(httpd_req_t *req);

/**
 * @brief Handler HTTP GET para listar todos los registros
 * 
 * Endpoint: GET /api/register
 * Response JSON: [{"id":1,"hour":14,"minute":30,"days":[...]}]
 * 
 * @param req Request HTTP del servidor
 * @return ESP_OK si se procesó correctamente
 */
esp_err_t api_get_registers(httpd_req_t *req);

/**
 * @brief Handler HTTP DELETE para eliminar registro
 * 
 * Endpoint: DELETE /api/register?id=5
 * 
 * @param req Request HTTP del servidor
 * @return ESP_OK si se eliminó correctamente
 */
esp_err_t api_delete_register(httpd_req_t *req);

/**
 * @brief Registrar todos los endpoints REST en el servidor HTTP
 * 
 * Debe llamarse durante la configuración del servidor HTTP para
 * habilitar los endpoints /api/register.
 * 
 * @param server Handle del servidor HTTP configurado
 */
void register_registers_endpoints(httpd_handle_t server);
