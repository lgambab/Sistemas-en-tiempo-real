#include "web_server.h"
#include "gpio_driver.h"
#include "ntc_sensor.h"
#include <esp_http_server.h>
#include <esp_spiffs.h>
#include <esp_log.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

static const char *TAG = "WEB_SERVER";
static httpd_handle_t server = NULL;

// --- GESTIÓN DE SESIONES SIMPLE (IP based) ---
#define MAX_SESSIONS 5
#define SESSION_TIMEOUT_MS (3 * 60 * 1000)
#define VALID_USER "root"
#define VALID_PASS "matrix123"

typedef struct {
    char ip[16];
    int64_t last_activity;
    bool authenticated;
} session_t;

static session_t sessions[MAX_SESSIONS];

// Obtener IP del cliente desde la request
static void get_client_ip(httpd_req_t *req, char *ip_str, size_t len) {
    // Obtener la dirección remota usando la API de ESP-IDF
    struct sockaddr_in *addr = (struct sockaddr_in *)req->sess_ctx;
    if (addr && addr->sin_family == AF_INET) {
        inet_ntoa_r(addr->sin_addr, ip_str, len);
        return;
    }
    
    // Fallback: usar una IP genérica si no se puede obtener
    // En un entorno SoftAP, todas las conexiones vienen de la misma red
    // Por simplicidad, usamos una IP única basada en el puntero de la request
    snprintf(ip_str, len, "192.168.4.%d", (int)((uintptr_t)req % 255) + 1);
}

// Obtener tiempo actual en milisegundos
static int64_t get_time_ms(void) {
    return (int64_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

// Buscar o crear sesión para una IP
static session_t* find_or_create_session(const char *ip) {
    int64_t now = get_time_ms();
    
    // Buscar sesión existente
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (strcmp(sessions[i].ip, ip) == 0) {
            // Verificar timeout
            if (now - sessions[i].last_activity > SESSION_TIMEOUT_MS) {
                sessions[i].authenticated = false;
                ESP_LOGI(TAG, "Sesión expirada para IP %s", ip);
            }
            return &sessions[i];
        }
    }
    
    // Buscar slot libre
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (!sessions[i].authenticated || (now - sessions[i].last_activity > SESSION_TIMEOUT_MS)) {
            strncpy(sessions[i].ip, ip, sizeof(sessions[i].ip) - 1);
            sessions[i].ip[sizeof(sessions[i].ip) - 1] = '\0';
            sessions[i].last_activity = now;
            sessions[i].authenticated = false;
            return &sessions[i];
        }
    }
    
    return NULL; // No hay slots disponibles
}

bool is_authenticated(httpd_req_t *req) {
    char ip[16] = {0};
    get_client_ip(req, ip, sizeof(ip));
    
    session_t *session = find_or_create_session(ip);
    if (!session) {
        return false;
    }
    
    if (!session->authenticated) {
        return false;
    }
    
    // Actualizar última actividad
    int64_t now = get_time_ms();
    if (now - session->last_activity > SESSION_TIMEOUT_MS) {
        session->authenticated = false;
        gpio_set_yellow(false);
        gpio_set_blue(false);
        ESP_LOGI(TAG, "Sesión expirada para IP %s. LEDs apagados.", ip);
        return false;
    }
    
    session->last_activity = now;
    return true;
}

// --- UTILIDAD PARA SERVIR ARCHIVOS DESDE SPIFFS ---
esp_err_t send_file_from_spiffs(httpd_req_t *req, const char *filepath, const char *type) {
    ESP_LOGI(TAG, "Intentando abrir archivo: %s", filepath);
    FILE *fd = fopen(filepath, "r");
    if (!fd) {
        ESP_LOGE(TAG, "No se encuentra el archivo: %s (errno: %d)", filepath, errno);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, type);
    char chunk[1024];
    size_t chunksize;
    size_t total_sent = 0;
    while ((chunksize = fread(chunk, 1, sizeof(chunk), fd)) > 0) {
        if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK) {
            ESP_LOGE(TAG, "Error al enviar chunk del archivo: %s", filepath);
            fclose(fd);
            return ESP_FAIL;
        }
        total_sent += chunksize;
    }
    fclose(fd);
    httpd_resp_send_chunk(req, NULL, 0); // Finalizar respuesta
    ESP_LOGI(TAG, "Archivo enviado exitosamente: %s (%d bytes)", filepath, total_sent);
    return ESP_OK;
}

// --- HANDLERS ---

// GET / (Raíz)
esp_err_t root_get_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "GET / - URI: %s", req->uri);
    esp_err_t ret;
    if (is_authenticated(req)) {
        ESP_LOGI(TAG, "Usuario autenticado, sirviendo index.html");
        ret = send_file_from_spiffs(req, "/spiffs/index.html", "text/html");
    } else {
        ESP_LOGI(TAG, "Usuario no autenticado, sirviendo login.html");
        ret = send_file_from_spiffs(req, "/spiffs/login.html", "text/html");
    }
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error al servir archivo HTML desde SPIFFS");
        // Enviar una respuesta de error básica
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, "<html><body><h1>Error: No se pudo cargar la página</h1></body></html>", HTTPD_RESP_USE_STRLEN);
    }
    
    return ret;
}

// GET /style.css
esp_err_t style_get_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "GET /style.css");
    return send_file_from_spiffs(req, "/spiffs/style.css", "text/css");
}

// GET /script.js
esp_err_t script_get_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "GET /script.js");
    return send_file_from_spiffs(req, "/spiffs/script.js", "application/javascript");
}

// GET /favicon.ico (evitar 404)
esp_err_t favicon_get_handler(httpd_req_t *req) {
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// GET /temperature
esp_err_t temperature_get_handler(httpd_req_t *req) {
    if (!is_authenticated(req)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "Unauthorized", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    
    // Obtener la temperatura actual almacenada por la tarea
    ntc_data_t temp_data = ntc_get_current_temperature();
    char response[128];
    int len;
    
    // Enviar la temperatura si es válida (no es -999.0)
    if (temp_data.temperature_c < -900.0 || isnan(temp_data.temperature_c) || !isfinite(temp_data.temperature_c)) {
        // Error en la lectura o datos no disponibles aún
        len = snprintf(response, sizeof(response), "{\"error\":\"No data available\"}");
    } else {
        // Enviar la temperatura siempre que sea un número válido
        // Usar formato más explícito para asegurar que sea JSON válido
        len = snprintf(response, sizeof(response), "{\"temperature\":%.1f}", temp_data.temperature_c);
        
        // Verificar que el JSON se formateó correctamente
        if (len >= sizeof(response)) {
            ESP_LOGE(TAG, "Buffer de respuesta demasiado pequeño!");
            len = snprintf(response, sizeof(response), "{\"error\":\"Buffer overflow\"}");
        }
    }
    
    // Configurar headers antes de enviar
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Expires", "0");
    
    esp_err_t ret = httpd_resp_send(req, response, len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error al enviar respuesta de temperatura: %s", esp_err_to_name(ret));
        return ret;
    }
    
    return ESP_OK;
}

// Decodificar URL (simple, solo espacios)
static void url_decode(char *str) {
    char *src = str;
    char *dst = str;
    while (*src) {
        if (*src == '%' && src[1] && src[2]) {
            // Decodificar %20 a espacio
            if (src[1] == '2' && src[2] == '0') {
                *dst++ = ' ';
                src += 3;
            } else {
                *dst++ = *src++;
            }
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

// GET /cmd?c=comando
esp_err_t cmd_get_handler(httpd_req_t *req) {
    char ip[16] = {0};
    get_client_ip(req, ip, sizeof(ip));
    
    if (!is_authenticated(req)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "[ERROR] No estás autenticado o la sesión ha expirado.", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    
    char buf[200];
    size_t buf_len = httpd_req_get_url_query_len(req) + 1;
    
    if (buf_len > 1 && buf_len < sizeof(buf)) {
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            char cmd[100];
            if (httpd_query_key_value(buf, "c", cmd, sizeof(cmd)) == ESP_OK) {
                url_decode(cmd);
                
                // Convertir a minúsculas
                for (int i = 0; cmd[i]; i++) {
                    if (cmd[i] >= 'A' && cmd[i] <= 'Z') {
                        cmd[i] = cmd[i] - 'A' + 'a';
                    }
                }
                
                // Procesar comandos
                char response[512] = {0};
                
                if (strcmp(cmd, "led y on") == 0) {
                    gpio_set_yellow(true);
                    strcpy(response, "[OK] LED amarillo encendido.");
                } else if (strcmp(cmd, "led y off") == 0) {
                    gpio_set_yellow(false);
                    strcpy(response, "[OK] LED amarillo apagado.");
                } else if (strcmp(cmd, "led b on") == 0) {
                    gpio_set_blue(true);
                    strcpy(response, "[OK] LED azul encendido.");
                } else if (strcmp(cmd, "led b off") == 0) {
                    gpio_set_blue(false);
                    strcpy(response, "[OK] LED azul apagado.");
                } else if (strcmp(cmd, "led all on") == 0) {
                    gpio_set_yellow(true);
                    gpio_set_blue(true);
                    strcpy(response, "[OK] Ambos LEDs encendidos.");
                } else if (strcmp(cmd, "led all off") == 0) {
                    gpio_set_yellow(false);
                    gpio_set_blue(false);
                    strcpy(response, "[OK] Ambos LEDs apagados.");
                } else if (strcmp(cmd, "status") == 0) {
                    const char *estadoAmarillo = gpio_get_yellow() ? "ON" : "OFF";
                    const char *estadoAzul = gpio_get_blue() ? "ON" : "OFF";
                    snprintf(response, sizeof(response), 
                             "Estado de los LEDs:\n  - Amarillo: %s\n  - Azul:     %s",
                             estadoAmarillo, estadoAzul);
                } else if (strcmp(cmd, "help") == 0) {
                    strcpy(response, 
                           "Comandos disponibles:\n\n"
                           "  --- Control Individual ---\n"
                           "  led y on          - Enciende el LED amarillo.\n"
                           "  led y off         - Apaga el LED amarillo.\n"
                           "  led b on          - Enciende el LED azul.\n"
                           "  led b off         - Apaga el LED azul.\n\n"
                           "  --- Control General ---\n"
                           "  led all on        - Enciende ambos LEDs.\n"
                           "  led all off       - Apaga ambos LEDs.\n\n"
                           "  --- Sistema ---\n"
                           "  status            - Muestra el estado de los LEDs.\n"
                           "  help              - Muestra esta lista.\n"
                           "  clear             - Limpia la pantalla.");
                } else {
                    snprintf(response, sizeof(response), 
                             "[?] Comando no reconocido: '%s'. Escribe 'help' para ver la lista.", cmd);
                }
                
                httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
                return ESP_OK;
            }
        }
    }
    httpd_resp_send(req, "[?] No se recibió ningún comando.", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// POST /login
esp_err_t login_post_handler(httpd_req_t *req) {
    char content[200];
    size_t recv_size = sizeof(content) - 1;
    
    int ret = httpd_req_recv(req, content, recv_size);
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    content[ret] = '\0';
    
    // Parsear user=xxx&pass=yyy
    char user[50] = {0};
    char pass[50] = {0};
    
    char *user_start = strstr(content, "user=");
    char *pass_start = strstr(content, "pass=");
    
    if (user_start) {
        user_start += 5; // Saltar "user="
        char *user_end = strchr(user_start, '&');
        if (user_end) {
            size_t user_len = user_end - user_start;
            if (user_len < sizeof(user)) {
                strncpy(user, user_start, user_len);
                user[user_len] = '\0';
            }
        } else {
            strncpy(user, user_start, sizeof(user) - 1);
        }
        url_decode(user);
    }
    
    if (pass_start) {
        pass_start += 5; // Saltar "pass="
        strncpy(pass, pass_start, sizeof(pass) - 1);
        url_decode(pass);
    }
    
    // Validar credenciales
    if (strcmp(user, VALID_USER) == 0 && strcmp(pass, VALID_PASS) == 0) {
        char ip[16] = {0};
        get_client_ip(req, ip, sizeof(ip));
        
        session_t *session = find_or_create_session(ip);
        if (session) {
            session->authenticated = true;
            session->last_activity = get_time_ms();
            ESP_LOGI(TAG, "Login exitoso desde IP: %s", ip);
            httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        }
    }
    
    ESP_LOGW(TAG, "Intento de login fallido. User: %s", user);
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_send(req, "[ERROR] Usuario o contraseña incorrectos.", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// GET /logout
esp_err_t logout_get_handler(httpd_req_t *req) {
    char ip[16] = {0};
    get_client_ip(req, ip, sizeof(ip));
    
    session_t *session = find_or_create_session(ip);
    if (session) {
        session->authenticated = false;
    }
    
    gpio_set_yellow(false);
    gpio_set_blue(false);
    
    ESP_LOGI(TAG, "Logout desde IP: %s. LEDs apagados.", ip);
    
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// Handler catch-all para debugging (debe registrarse al final)
esp_err_t catch_all_handler(httpd_req_t *req) {
    ESP_LOGW(TAG, "URI no manejada: %s (método: %d)", req->uri, req->method);
    httpd_resp_send_404(req);
    return ESP_FAIL;
}

// --- INICIO ---
void start_webserver(void) {
    // 0. Inicializar sesiones
    memset(sessions, 0, sizeof(sessions));
    
    // 1. Iniciar SPIFFS
    // Primero intentar montar sin formatear
    esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs",
      .partition_label = "storage",
      .max_files = 5,
      .format_if_mount_failed = false  // NO formatear automáticamente
    };
    
    ESP_LOGI(TAG, "Inicializando SPIFFS desde partición 'storage'...");
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    
    // Si falla el montaje, intentar formatear solo si es necesario
    if (ret == ESP_FAIL) {
        ESP_LOGW(TAG, "SPIFFS no se pudo montar, intentando formatear...");
        conf.format_if_mount_failed = true;
        ret = esp_vfs_spiffs_register(&conf);
        if (ret == ESP_OK) {
            ESP_LOGW(TAG, "SPIFFS formateado - los archivos deben ser flasheados nuevamente");
        }
    }
    
    if (ret != ESP_OK) {
        if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Partición SPIFFS 'storage' no encontrada");
            ESP_LOGE(TAG, "Verifica que la tabla de particiones incluya una partición 'storage' tipo spiffs");
        } else if (ret == ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "SPIFFS ya está montado o no se puede desmontar");
        } else {
            ESP_LOGE(TAG, "Error al inicializar SPIFFS: %s (%d)", esp_err_to_name(ret), ret);
        }
        return;
    }
    ESP_LOGI(TAG, "SPIFFS montado correctamente en /spiffs");
    
    // Obtener información de SPIFFS
    size_t total = 0, used = 0;
    ret = esp_spiffs_info("storage", &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error al obtener información de SPIFFS (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "SPIFFS: %d KB total, %d KB usado (%.1f%%)", 
                 total / 1024, used / 1024, (used * 100.0f) / total);
    }
    
    // Verificar que los archivos existan en SPIFFS
    ESP_LOGI(TAG, "Verificando archivos en SPIFFS...");
    const char *files_to_check[] = {"/spiffs/index.html", "/spiffs/login.html", 
                                    "/spiffs/style.css", "/spiffs/script.js"};
    int files_found = 0;
    for (int i = 0; i < 4; i++) {
        FILE *f = fopen(files_to_check[i], "r");
        if (f) {
            // Obtener tamaño del archivo
            fseek(f, 0, SEEK_END);
            long size = ftell(f);
            fclose(f);
            ESP_LOGI(TAG, "✓ Archivo encontrado: %s (%ld bytes)", files_to_check[i], size);
            files_found++;
        } else {
            ESP_LOGE(TAG, "✗ Archivo NO encontrado: %s (errno: %d - %s)", 
                     files_to_check[i], errno, strerror(errno));
        }
    }
    
    if (files_found == 0) {
        ESP_LOGE(TAG, "==========================================");
        ESP_LOGE(TAG, "ERROR CRÍTICO: Ningún archivo encontrado en SPIFFS");
        ESP_LOGE(TAG, "Los archivos deben ser flasheados a la partición SPIFFS");
        ESP_LOGE(TAG, "Ejecuta: ./flash_all.sh [PORT] o idf.py flash");
        ESP_LOGE(TAG, "==========================================");
    } else if (files_found < 4) {
        ESP_LOGW(TAG, "Advertencia: Solo %d de 4 archivos encontrados", files_found);
    } else {
        ESP_LOGI(TAG, "✓ Todos los archivos encontrados correctamente (%d/4)", files_found);
    }

    // 2. Configurar Server
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 10;
    config.max_open_sockets = 7;
    config.lru_purge_enable = true; // Habilitar purga de conexiones inactivas

    esp_err_t httpd_ret = httpd_start(&server, &config);
    if (httpd_ret != ESP_OK) {
        ESP_LOGE(TAG, "Error al iniciar el servidor web: %s", esp_err_to_name(httpd_ret));
        return;
    }
    
    ESP_LOGI(TAG, "Servidor HTTP iniciado, registrando rutas...");
    
    // Registrar rutas
    httpd_uri_t root_uri = { 
        .uri = "/", 
        .method = HTTP_GET, 
        .handler = root_get_handler,
        .user_ctx = NULL
    };
    if (httpd_register_uri_handler(server, &root_uri) != ESP_OK) {
        ESP_LOGE(TAG, "Error al registrar ruta /");
    } else {
        ESP_LOGI(TAG, "Ruta / registrada");
    }

    httpd_uri_t css_uri = { 
        .uri = "/style.css", 
        .method = HTTP_GET, 
        .handler = style_get_handler,
        .user_ctx = NULL
    };
    if (httpd_register_uri_handler(server, &css_uri) != ESP_OK) {
        ESP_LOGE(TAG, "Error al registrar ruta /style.css");
    } else {
        ESP_LOGI(TAG, "Ruta /style.css registrada");
    }
    
    httpd_uri_t js_uri = { 
        .uri = "/script.js", 
        .method = HTTP_GET, 
        .handler = script_get_handler,
        .user_ctx = NULL
    };
    if (httpd_register_uri_handler(server, &js_uri) != ESP_OK) {
        ESP_LOGE(TAG, "Error al registrar ruta /script.js");
    } else {
        ESP_LOGI(TAG, "Ruta /script.js registrada");
    }

    httpd_uri_t cmd_uri = { 
        .uri = "/cmd", 
        .method = HTTP_GET, 
        .handler = cmd_get_handler,
        .user_ctx = NULL
    };
    if (httpd_register_uri_handler(server, &cmd_uri) != ESP_OK) {
        ESP_LOGE(TAG, "Error al registrar ruta /cmd");
    } else {
        ESP_LOGI(TAG, "Ruta /cmd registrada");
    }

    httpd_uri_t login_uri = { 
        .uri = "/login", 
        .method = HTTP_POST, 
        .handler = login_post_handler,
        .user_ctx = NULL
    };
    if (httpd_register_uri_handler(server, &login_uri) != ESP_OK) {
        ESP_LOGE(TAG, "Error al registrar ruta /login");
    } else {
        ESP_LOGI(TAG, "Ruta /login registrada");
    }
    
    httpd_uri_t logout_uri = { 
        .uri = "/logout", 
        .method = HTTP_GET, 
        .handler = logout_get_handler,
        .user_ctx = NULL
    };
    if (httpd_register_uri_handler(server, &logout_uri) != ESP_OK) {
        ESP_LOGE(TAG, "Error al registrar ruta /logout");
    } else {
        ESP_LOGI(TAG, "Ruta /logout registrada");
    }

    httpd_uri_t temperature_uri = { 
        .uri = "/temperature", 
        .method = HTTP_GET, 
        .handler = temperature_get_handler,
        .user_ctx = NULL
    };
    if (httpd_register_uri_handler(server, &temperature_uri) != ESP_OK) {
        ESP_LOGE(TAG, "Error al registrar ruta /temperature");
    } else {
        ESP_LOGI(TAG, "Ruta /temperature registrada");
    }

    // Handler para favicon.ico (evitar 404)
    httpd_uri_t favicon_uri = {
        .uri = "/favicon.ico",
        .method = HTTP_GET,
        .handler = favicon_get_handler,
        .user_ctx = NULL
    };
    if (httpd_register_uri_handler(server, &favicon_uri) != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo registrar /favicon.ico (no crítico)");
    } else {
        ESP_LOGI(TAG, "Ruta /favicon.ico registrada");
    }
    
    // Registrar handler catch-all con wildcard (debe ser el último)
    // Nota: Para usar wildcard, necesitamos habilitar uri_match_fn
    // Por ahora, no lo registramos para evitar conflictos
    // httpd_uri_t catch_all_uri = {
    //     .uri = "/*",
    //     .method = HTTP_GET,
    //     .handler = catch_all_handler,
    //     .user_ctx = NULL
    // };
    
    ESP_LOGI(TAG, "Servidor Web Iniciado correctamente en puerto %d", config.server_port);
    ESP_LOGI(TAG, "Total de handlers registrados: 7 (/, /style.css, /script.js, /cmd, /login, /logout, /temperature)");
}