/**
 * @file http_server.h
 * @brief Servidor HTTP con endpoints REST para control del sistema
 * @author Jair Hernan Telpis Cuaran, Luis Fernando Gamba Bedoya
 * @date 2025
 * @version 1.0.0
 * 
 * @details Servidor HTTP embebido que proporciona:
 * - **Interfaz Web Completa**: HTML/CSS/JS para control del sistema
 * - **Endpoints REST**: Control de ventilador, registros, sensores
 * - **OTA Updates**: Actualización de firmware vía HTTP
 * - **Monitoreo de Estado**: Cola de mensajes para eventos WiFi y sistema
 * - **Archivos Embebidos**: index.html, app.js, app.css, jQuery, favicon
 * 
 * **Principales Endpoints:**
 * - GET  / → Página web principal
 * - GET  /dhtSensor.json → Leer temperatura y PIR
 * - POST /fanSettings.json → Configurar ventilador
 * - POST /regchange.json → Modificar registro programado
 * - GET  /api/register → API de registros (ver registers.h)
 * - POST /OTAupdate → Actualización OTA de firmware
 * 
 * El servidor se inicializa automáticamente cuando WiFi se conecta y mantiene
 * una tarea monitor que procesa mensajes de estado del sistema.
 * 
 * Universidad Nacional de Colombia - Curso RTOS
 */

#ifndef MAIN_HTTP_SERVER_H_
#define MAIN_HTTP_SERVER_H_
#include "freertos/FreeRTOS.h"
#include "esp_event.h"
#include "esp_adc/adc_oneshot.h"

/** @defgroup OTA_Status Estados de actualización OTA
 *  @{
 */
#define OTA_UPDATE_PENDING 		0	/**< Actualización OTA pendiente */
#define OTA_UPDATE_SUCCESSFUL	1	/**< Actualización OTA exitosa */
#define OTA_UPDATE_FAILED		-1	/**< Actualización OTA fallida */
/** @} */

#define BLINK_GPIO				2	/**< GPIO del LED de estado */

/**
 * @enum http_server_wifi_connect_status_e
 * @brief Estados de conexión WiFi reportados al servidor HTTP
 */
typedef enum http_server_wifi_connect_status
{
	NONE = 0,								/**< Estado inicial, sin conexión */
	HTTP_WIFI_STATUS_CONNECTING,			/**< Intentando conectar a WiFi */
	HTTP_WIFI_STATUS_CONNECT_FAILED,		/**< Conexión WiFi falló */
	HTTP_WIFI_STATUS_CONNECT_SUCCESS,		/**< Conexión WiFi exitosa */
} http_server_wifi_connect_status_e;

/**
 * @enum http_server_message_e
 * @brief Mensajes para la tarea monitor del servidor HTTP
 * 
 * Estos mensajes permiten comunicación asíncrona entre el sistema WiFi
 * y el servidor HTTP, manejando eventos críticos como conexión/desconexión
 * y actualizaciones OTA.
 */
typedef enum http_server_message
{
	HTTP_MSG_WIFI_CONNECT_INIT = 0,		/**< Iniciar conexión WiFi */
	HTTP_MSG_WIFI_CONNECT_SUCCESS,		/**< WiFi conectado exitosamente */
	HTTP_MSG_WIFI_CONNECT_FAIL,			/**< Error al conectar WiFi */
	HTTP_MSG_OTA_UPDATE_SUCCESSFUL,		/**< Actualización OTA completada */
	HTTP_MSG_OTA_UPDATE_FAILED,			/**< Actualización OTA falló */
} http_server_message_e;

/**
 * @struct http_server_queue_message_t
 * @brief Estructura de mensajes para la cola del monitor HTTP
 */
typedef struct http_server_queue_message
{
	http_server_message_e msgID;	/**< ID del mensaje a procesar */
} http_server_queue_message_t;

/**
 * @brief Enviar mensaje a la cola del monitor HTTP
 * 
 * Permite comunicación thread-safe entre módulos del sistema y el servidor HTTP.
 * Los mensajes se procesan en orden FIFO por la tarea http_server_monitor.
 * 
 * @param msgID ID del mensaje desde http_server_message_e
 * @return pdTRUE si el mensaje se envió correctamente
 * @return pdFALSE si la cola está llena (espera indefinida activada)
 * 
 * @note Esta función usa portMAX_DELAY, bloqueará hasta que haya espacio en la cola
 */
BaseType_t http_server_monitor_send_message(http_server_message_e msgID);

/**
 * @brief Iniciar servidor HTTP y tareas relacionadas
 * 
 * Crea el servidor HTTP en el puerto 80, registra todos los handlers
 * de endpoints REST, inicializa sensores (NTC, PIR) y crea las tareas:
 * - http_server_monitor: Procesa mensajes de estado
 * - fan_task: Actualiza control del ventilador cada 1s
 * 
 * @note Solo se ejecuta si el servidor no está ya iniciado
 * @note Debe llamarse después de que WiFi esté configurado
 * 
 * @see http_server_stop() para detener el servidor
 */
void http_server_start(void);

/**
 * @brief Detener servidor HTTP y liberar recursos
 * 
 * Detiene el servidor HTTP y elimina la tarea monitor asociada.
 * Usado típicamente al cambiar configuración WiFi o antes de reiniciar.
 */
void http_server_stop(void);

/**
 * @brief Callback de timer para reinicio tras OTA exitoso
 * 
 * Se ejecuta 8 segundos después de una actualización OTA exitosa,
 * dando tiempo a que el navegador reciba la respuesta HTTP antes de reiniciar.
 * 
 * @param arg Argumento del timer (sin usar)
 * 
 * @note Llama a esp_restart() para aplicar nuevo firmware
 */
void http_server_fw_update_reset_callback(void *arg);

/**
 * @brief Alternar LED de estado
 * 
 * Cambia el estado del LED en BLINK_GPIO. Usado para indicadores visuales
 * durante operaciones del sistema.
 */
void toogle_led(void);

#endif /* MAIN_HTTP_SERVER_H_ */
