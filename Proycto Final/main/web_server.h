#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Inicia el sistema de archivos (SPIFFS) y levanta el servidor HTTP
void start_webserver(void);

// Si necesitaras detenerlo desde otro lado, podrías añadir:
// void stop_webserver(void);

#ifdef __cplusplus
}
#endif