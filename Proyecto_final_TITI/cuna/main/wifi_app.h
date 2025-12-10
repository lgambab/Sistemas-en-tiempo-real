/**
 * @file wifi_app.h
 * @brief Gestión completa de WiFi AP/STA con reconexión automática y persistencia
 * @author Jair Hernan Telpis Cuaran, Luis Fernando Gamba Bedoya
 * @date 2025
 * @version 1.0.0
 * 
 * @details Módulo que gestiona la configuración dual WiFi (Access Point + Station) del ESP32:
 * - **Access Point (AP)**: Crea red WiFi para configuración inicial
 * - **Station (STA)**: Se conecta a redes WiFi existentes
 * - **Reconexión Automática**: Reintenta conexión hasta MAX_CONNECTION_RETRIES veces
 * - **Persistencia NVS**: Guarda credenciales WiFi para reconexión tras reinicio
 * - **Sincronización SNTP**: Obtiene hora de servidores NTP cuando se conecta
 * - **Cola de Mensajes RTOS**: Comunicación thread-safe con el servidor HTTP
 * 
 * El sistema inicia en modo AP+STA, permitiendo configuración web mientras mantiene
 * el AP activo. Las credenciales se guardan en NVS y se cargan automáticamente al reiniciar.
 * 
 * Universidad Nacional de Colombia - Curso RTOS
 */

#ifndef MAIN_WIFI_APP_H_
#define MAIN_WIFI_APP_H_

#include "esp_netif.h"
#include "esp_wifi_types.h"
#include "freertos/FreeRTOS.h"

// Callback typedef
typedef void (*wifi_connected_event_callback_t)(void);

/** @defgroup WiFi_Config Configuración del Access Point
 *  @{
 */
#define WIFI_AP_SSID				"Mi_esp"			/**< Nombre de la red AP */
#define WIFI_AP_PASSWORD			"12345678"			/**< Contraseña del AP (mínimo 8 caracteres) */
#define WIFI_AP_CHANNEL				1					/**< Canal WiFi del AP (1-13) */
#define WIFI_AP_SSID_HIDDEN			0					/**< 0=visible, 1=oculto */
#define WIFI_AP_MAX_CONNECTIONS		5					/**< Máximo de clientes simultáneos */
#define WIFI_AP_BEACON_INTERVAL		100					/**< Intervalo beacon en ms (recomendado 100ms) */
#define WIFI_AP_IP					"192.168.0.1"		/**< IP del AP */
#define WIFI_AP_GATEWAY				"192.168.0.1"		/**< Gateway del AP (igual que IP) */
#define WIFI_AP_NETMASK				"255.255.255.0"		/**< Máscara de subred */
#define WIFI_AP_BANDWIDTH			WIFI_BW_HT20		/**< Ancho de banda 20 MHz */
#define WIFI_STA_POWER_SAVE			WIFI_PS_NONE		/**< Sin ahorro de energía */
#define MAX_SSID_LENGTH				32					/**< Longitud máxima SSID (estándar IEEE) */
#define MAX_PASSWORD_LENGTH			64					/**< Longitud máxima contraseña (estándar IEEE) */
#define MAX_CONNECTION_RETRIES		5					/**< Intentos de reconexión antes de fallar */
/** @} */

#define NUM_REGISTERS_AV 			10					/**< Número máximo de registros programados */

/** @brief Handle de interfaz de red Station */
extern esp_netif_t* esp_netif_sta;
/** @brief Handle de interfaz de red Access Point */
extern esp_netif_t* esp_netif_ap;

/**
 * @enum wifi_app_message_e
 * @brief IDs de mensajes para la cola de la tarea WiFi
 * 
 * Estos mensajes permiten comunicación asíncrona entre el servidor HTTP
 * y la tarea WiFi, evitando bloqueos y garantizando thread-safety.
 */
typedef enum wifi_app_message
{
	WIFI_APP_MSG_START_HTTP_SERVER = 0,			/**< Iniciar servidor HTTP tras conexión WiFi */
	WIFI_APP_MSG_CONNECTING_FROM_HTTP_SERVER,		/**< Usuario solicitó conexión desde web */
	WIFI_APP_MSG_STA_CONNECTED_GOT_IP,				/**< STA conectado y obtuvo IP por DHCP */
	WIFI_APP_MSG_USER_REQUESTED_STA_DISCONNECT,	/**< Usuario solicitó desconexión desde web */
	WIFI_APP_MSG_LOAD_SAVED_CREDENTIALS,			/**< Cargar credenciales guardadas en NVS */
	WIFI_APP_MSG_STA_DISCONNECTED,					/**< STA desconectado (evento del sistema) */
	WIFI_APP_CONNECT_TO_STA,						/**< Comando para conectar a STA */
} wifi_app_message_e;

/**
 * @struct wifi_app_queue_message_t
 * @brief Estructura de mensajes para la cola WiFi
 */
typedef struct wifi_app_queue_message
{
	wifi_app_message_e msgID;	/**< ID del mensaje a procesar */
} wifi_app_queue_message_t;

/**
 * @struct register_saved_e
 * @brief Estructura de registro programado guardado en NVS
 * 
 * Almacena hora, minuto y días de la semana para ejecución programada.
 */
typedef struct register_saved
{
	uint8_t hour;		/**< Hora de ejecución (0-23) */
	uint8_t min;		/**< Minuto de ejecución (0-59) */
	uint8_t monday;		/**< 1=activo el lunes, 0=inactivo */
	uint8_t tuesday;	/**< 1=activo el martes, 0=inactivo */
	uint8_t wednesday;	/**< 1=activo el miércoles, 0=inactivo */
	uint8_t thursday;	/**< 1=activo el jueves, 0=inactivo */
	uint8_t friday;		/**< 1=activo el viernes, 0=inactivo */
	uint8_t sunday;		/**< 1=activo el domingo, 0=inactivo */
	uint8_t saturday;	/**< 1=activo el sábado, 0=inactivo */
} register_saved_e;

/**
 * @brief Obtener hora desde servidor SNTP
 * @details Sincroniza reloj del sistema con pool.ntp.org
 * @note Función interna, llamada automáticamente tras conexión WiFi
 */
static void obtain_time(void);

/**
 * @brief Conectar a WiFi usando credenciales guardadas
 * @details Lee credenciales de NVS e intenta conexión STA
 */
void connect_to_wifi(void);

/**
 * @brief Inicializar sistema de registros programados
 * @details Carga registros desde NVS al arranque
 */
void initialize_registers(void);

/**
 * @brief Leer datos de registro desde NVS
 * @param[out] str_to_save Buffer donde se escribirá el registro (JSON)
 * @param register_num Número de registro (1-10)
 * @return ESP_OK si se leyó correctamente
 */
esp_err_t read_reg_data(char *str_to_save, uint8_t register_num);

/**
 * @brief Inicializar sincronización de hora SNTP
 * @details Configura servidor NTP y zona horaria
 */
void init_obtain_time(void);

/**
 * @brief Verificar si la hora ya fue sincronizada
 * @return true si el sistema tiene hora válida de SNTP
 */
bool get_state_time_was_synchronized(void);

/**
 * @brief Guardar registro en NVS
 * @param register Número de registro (1-10)
 * @param str Datos del registro en formato string
 */
void save_reg_data(uint8_t register, char *str);

/**
 * @brief Guardar credenciales WiFi en NVS
 * @param ssid Nombre de red WiFi
 * @param password Contraseña de red WiFi
 * @note Las credenciales persisten tras reinicios
 */
void save_wifi_credentials(const char *ssid, const char *password);

/**
 * @brief Cargar credenciales WiFi desde NVS
 * @param[out] ssid Buffer para SSID (mínimo MAX_SSID_LENGTH)
 * @param[out] password Buffer para contraseña (mínimo MAX_PASSWORD_LENGTH)
 */
void load_wifi_credentials(char *ssid, char *password);

/**
 * @brief Conectar ESP32 a red WiFi como Station
 * @note Función interna, usa credenciales cargadas previamente
 */
static void wifi_app_connect_sta(void);

/**
 * @brief Tarea que verifica estado de conexión STA
 * @param pvParameters Parámetros de la tarea (sin usar)
 * @details Monitorea conexión y reintenta automáticamente si falla
 */
void check_sta_connection_state(void *pvParameters);

/**
 * @brief Tarea que compara hora actual con registros programados
 * @param pvParameters Parámetros de la tarea (sin usar)
 * @details Ejecuta cada minuto, activa ventilador si hay coincidencia horaria
 */
void task_compare_hour_to_execute_action(void *pvParameters);

/**
 * @brief Actualizar registro específico
 * @param reg_to_update Número de registro a actualizar (1-10)
 */
void update_register(int reg_to_update);

/**
 * @brief Enviar mensaje a la cola de la tarea WiFi
 * @param msgID ID del mensaje desde wifi_app_message_e
 * @return pdTRUE si se envió correctamente, pdFALSE si la cola está llena
 * @note Thread-safe, puede llamarse desde cualquier tarea
 */
BaseType_t wifi_app_send_message(wifi_app_message_e msgID);

/**
 * @brief Iniciar tarea RTOS de WiFi
 * @details Crea la tarea principal que gestiona eventos WiFi y mensajes
 * @note Debe llamarse una vez durante la inicialización del sistema
 */
void wifi_app_start(void);

/**
 * @brief Obtener configuración actual de WiFi
 * @return Puntero a estructura wifi_config_t con configuración STA actual
 */
wifi_config_t* wifi_app_get_wifi_config(void);

/**
 * @brief Registrar callback para evento de conexión exitosa
 * @param cb Función callback a ejecutar cuando STA se conecta
 */
void wifi_app_set_callback(wifi_connected_event_callback_t cb);

/**
 * @brief Ejecutar callback registrado
 * @details Llamado internamente cuando STA obtiene IP
 */
void wifi_app_call_callback(void);

/**
 * @brief Obtener intensidad de señal WiFi (RSSI)
 * @return Nivel RSSI actual en dBm (valores negativos, ej: -50 dBm)
 * @note Solo válido cuando STA está conectado
 */
int8_t wifi_app_get_rssi(void);

#endif /* MAIN_WIFI_APP_H_ */




























