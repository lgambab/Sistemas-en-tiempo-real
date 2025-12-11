// auth_display.h
// Sistema de autenticación con teclado y display de información en OLED
#ifndef AUTH_DISPLAY_H
#define AUTH_DISPLAY_H

#include "esp_err.h"
#include <stdbool.h>

// Inicializar sistema de autenticación
esp_err_t auth_display_init(void);

// Procesar tecla presionada (llamar desde peripherals cuando hay evento de tecla)
void auth_display_process_key(char key);

// Configurar contraseña (por defecto "1234")
esp_err_t auth_display_set_password(const char* password);

// Obtener estado del display (true si alguien está viendo la pantalla)
bool auth_display_is_active(void);

#endif // AUTH_DISPLAY_H
