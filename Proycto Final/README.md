# Project 7: Retro Web Terminal

Terminal web retro estilo Matrix para ESP32 con autenticación, control de LEDs y servidor web embebido.

## Descripción

El proyecto implementa un servidor web retro estilo terminal en un ESP32 que permite:
- **Acceso Point WiFi (SoftAP)**: El ESP32 crea su propia red WiFi
- **Interfaz Web Retro**: Terminal estilo Matrix con estética retro
- **Autenticación**: Sistema de login con sesiones basadas en IP
- **Control de Hardware**: Comandos para controlar LEDs (amarillo y azul)
- **Almacenamiento SPIFFS**: Archivos HTML/CSS/JS almacenados en partición SPIFFS

## Instrucciones de Flasheo

### Requisitos Previos

1. **ESP-IDF instalado y configurado**
   ```bash
   # Si no se tiene ESP-IDF instalado, se debe seguir la guía oficial:
   # https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/
   ```

2. **Hardware necesario**
   - ESP32 (cualquier variante compatible)
   - Cable USB para programación
   - LEDs conectados a los GPIOs configurados (opcional)

### Método 1: Script Automático (Recomendado)

El script `flash_all.sh` automatiza todo el proceso de flasheo:

```bash
# 1. Dar permisos de ejecución (solo la primera vez)
chmod +x flash_all.sh

# 2. Flashear el proyecto completo (incluyendo SPIFFS)
./flash_all.sh /dev/ttyUSB0

# Si el puerto es diferente, se debe especificar:
./flash_all.sh /dev/ttyACM0
```

**¿Qué hace el script?**
- Activa automáticamente el entorno ESP-IDF
- Construye el proyecto (`idf.py build`)
- Verifica que `storage.bin` se haya generado correctamente
- Flashea bootloader, particiones y aplicación
- Flashea la partición SPIFFS con los archivos web

### Método 2: Manual

Para realizar el flasheo paso a paso:

```bash
# 1. Activar entorno ESP-IDF
. $HOME/esp/esp-idf/export.sh

# 2. Construir el proyecto
idf.py build

# 3. Flashear firmware (bootloader, particiones, aplicación)
idf.py -p /dev/ttyUSB0 flash

# 4. Flashear SPIFFS (archivos web)
idf.py -p /dev/ttyUSB0 storage-flash

# O usando esptool.py directamente:
esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 921600 \
  --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_freq 40m --flash_size 4MB \
  0x110000 build/storage.bin
```

### Verificación Post-Flasheo

Después de flashear, se debe ejecutar el monitor para verificar:

```bash
idf.py -p /dev/ttyUSB0 monitor
```

**Logs esperados:**
```
I (xxx) WIFI_APP: SoftAP iniciado. SSID: ESP32_Server Clave: 12345678 canal: 1
I (xxx) WEB_SERVER: SPIFFS montado correctamente en /spiffs
I (xxx) WEB_SERVER: SPIFFS: 1024 KB total, XX KB usado
I (xxx) WEB_SERVER: Archivo encontrado: /spiffs/index.html (XXX bytes)
I (xxx) WEB_SERVER: Archivo encontrado: /spiffs/login.html (XXX bytes)
I (xxx) WEB_SERVER: Archivo encontrado: /spiffs/style.css (XXX bytes)
I (xxx) WEB_SERVER: Archivo encontrado: /spiffs/script.js (XXX bytes)
I (xxx) WEB_SERVER: Todos los archivos encontrados correctamente (4/4)
I (xxx) WEB_SERVER: Servidor Web Iniciado correctamente en puerto 80
```

### Solución de Problemas

**Error: "Puerto ocupado"**
```bash
# Se debe cerrar cualquier monitor o programa usando el puerto
# Luego se debe intentar de nuevo
./flash_all.sh /dev/ttyUSB0
```

**Error: "Archivos NO encontrados en SPIFFS"**
```bash
# Se debe asegurar de flashear SPIFFS explícitamente:
idf.py -p /dev/ttyUSB0 storage-flash
```

**Error: "idf.py: command not found"**
```bash
# Se debe activar el entorno ESP-IDF:
. $HOME/esp/esp-idf/export.sh
# O el script lo hará automáticamente
```

## Diagrama de Flujo del Programa

```
┌─────────────────────────────────────────────────────────────────┐
│                    INICIO DEL PROGRAMA                         │
│                      (app_main)                                 │
└────────────────────────────┬────────────────────────────────────┘
                             │
                             ▼
                    ┌─────────────────┐
                    │ 1. Inicializar  │
                    │      NVS        │
                    │  (nvs_flash)    │
                    └────────┬────────┘
                             │
                             ▼
                    ┌─────────────────┐
                    │ 2. Inicializar  │
                    │    Hardware     │
                    │  (GPIO LEDs)    │
                    └────────┬────────┘
                             │
                             ▼
                    ┌─────────────────┐
                    │ 3. Inicializar  │
                    │   WiFi SoftAP   │
                    │  (wifi_app.c)   │
                    └────────┬────────┘
                             │
                    ┌────────┴────────┐
                    │ Crea red WiFi:  │
                    │ SSID: ESP32_    │
                    │      Server     │
                    │ Pass: 12345678  │
                    │ IP: 192.168.4.1 │
                    └────────┬────────┘
                             │
                             ▼
                    ┌─────────────────┐
                    │ 4. Inicializar   │
                    │  Servidor Web    │
                    │ (web_server.c)  │
                    └────────┬────────┘
                             │
                    ┌────────┴────────┐
                    │ 4.1. Montar     │
                    │     SPIFFS      │
                    │  (/spiffs)      │
                    └────────┬────────┘
                             │
                    ┌────────┴────────┐
                    │ 4.2. Verificar  │
                    │   archivos web  │
                    │ (HTML/CSS/JS)   │
                    └────────┬────────┘
                             │
                    ┌────────┴────────┐
                    │ 4.3. Iniciar    │
                    │  HTTP Server   │
                    │  (Puerto 80)    │
                    └────────┬────────┘
                             │
                             ▼
        ┌───────────────────────────────────────────────┐
        │         SERVIDOR WEB ACTIVO                  │
        │  Esperando conexiones en 192.168.4.1:80      │
        └───────────────────────────────────────────────┘
                             │
        ┌────────────────────┴────────────────────┐
        │                                           │
        ▼                                           ▼
┌───────────────┐                        ┌───────────────┐
│ Cliente se    │                        │ Cliente se    │
│ conecta a /   │                        │ conecta a /   │
└───────┬───────┘                        └───────┬───────┘
        │                                         │
        ▼                                         ▼
┌───────────────┐                        ┌───────────────┐
│ ¿Autenticado? │                        │ POST /login   │
└───────┬───────┘                        └───────┬───────┘
        │                                         │
   ┌────┴────┐                              ┌─────┴─────┐
   │ NO     │                              │ Validar   │
   │        │                              │ credenciales│
   ▼        │                              └─────┬─────┘
┌──────────┐│                              ┌─────┴─────┐
│ Servir   ││                              │ ¿Válidas? │
│login.html││                              └─────┬─────┘
└──────────┘│                              ┌─────┴─────┐
            │                              │ SÍ        │
            │                              │ Crear     │
            │                              │ sesión    │
            │                              └─────┬─────┘
            │                                    │
            └────────────────┬───────────────────┘
                             │
                             ▼
                    ┌─────────────────┐
                    │ Servir          │
                    │ index.html      │
                    │ (Terminal)      │
                    └────────┬────────┘
                             │
        ┌────────────────────┴────────────────────┐
        │                                           │
        ▼                                           ▼
┌───────────────┐                        ┌───────────────┐
│ GET /cmd?c=   │                        │ GET /style.css │
│ comando       │                        │ GET /script.js│
└───────┬───────┘                        └───────┬───────┘
        │                                         │
        ▼                                         ▼
┌───────────────┐                        ┌───────────────┐
│ Procesar      │                        │ Servir desde │
│ comando       │                        │ SPIFFS       │
└───────┬───────┘                        └───────────────┘
        │
   ┌────┴────┐
   │         │
   ▼         ▼
┌────────┐ ┌────────┐
│led y on│ │led b on│
│led y   │ │led b   │
│  off   │ │  off   │
└───┬────┘ └───┬────┘
    │          │
    └────┬─────┘
         │
         ▼
┌─────────────────┐
│ Controlar GPIOs │
│ (LEDs)          │
└─────────────────┘
```

## Estructura del Proyecto

```
Project_7_Retro_Web_Terminal/
├── CMakeLists.txt          # Configuración del proyecto y SPIFFS
├── partitions.csv          # Tabla de particiones (incluye SPIFFS)
├── sdkconfig              # Configuración de ESP-IDF
├── flash_all.sh           # Script automático de flasheo
├── README.md              # Archivo de documentación
│
├── front/                 # Archivos web (se copian a SPIFFS)
│   ├── index.html        # Terminal principal (requiere login)
│   ├── login.html        # Página de login
│   ├── style.css         # Estilos retro estilo Matrix
│   └── script.js         # Lógica del terminal web
│
└── main/                  # Código fuente del ESP32
    ├── CMakeLists.txt
    ├── main.c            # Punto de entrada (app_main)
    ├── wifi_app.c        # Configuración WiFi SoftAP
    ├── wifi_app.h        # Definiciones WiFi
    ├── web_server.c      # Servidor HTTP y lógica web
    ├── web_server.h      # Headers del servidor
    ├── gpio_driver.c     # Control de LEDs
    └── gpio_driver.h     # Headers GPIO
```

## Credenciales por Defecto

- **SSID WiFi**: `ESP32_Server`
- **Contraseña WiFi**: `12345678`
- **IP del ESP32**: `192.168.4.1`
- **Usuario Web**: `root`
- **Contraseña Web**: `matrix123`

> **Importante**: Se deben cambiar estas credenciales en producción.

## Comandos Disponibles

Una vez autenticado, el usuario puede usar los siguientes comandos en el terminal web:

### Control Individual de LEDs
- `led y on` - Enciende el LED amarillo
- `led y off` - Apaga el LED amarillo
- `led b on` - Enciende el LED azul
- `led b off` - Apaga el LED azul

### Control General
- `led all on` - Enciende ambos LEDs
- `led all off` - Apaga ambos LEDs

### Sistema
- `status` - Muestra el estado actual de los LEDs
- `help` - Muestra la lista de comandos disponibles
- `clear` - Limpia la pantalla del terminal

## Configuración

### Cambiar Credenciales WiFi

Se debe editar el archivo `main/wifi_app.h`:
```c
#define ESP_WIFI_SSID      "Tu_SSID"
#define ESP_WIFI_PASS      "Tu_Contraseña"
#define ESP_WIFI_CHANNEL   1
```

### Cambiar Credenciales Web

Se debe editar el archivo `main/web_server.c`:
```c
#define VALID_USER "tu_usuario"
#define VALID_PASS "tu_contraseña"
```

### Modificar Archivos Web

1. Se deben editar los archivos en `front/`
2. Se debe reconstruir y flashear:
   ```bash
   ./flash_all.sh /dev/ttyUSB0
   ```

## Tabla de Particiones

```
# Name,   Type, SubType, Offset,  Size
nvs,      data, nvs,     ,        0x6000
phy_init, data, phy,     ,        0x1000
factory,  app,  factory, ,        1M
storage,  data, spiffs,  ,        1M    ← Archivos web aquí
```

## Troubleshooting

### Los archivos web no se cargan
- Se debe verificar que SPIFFS se haya flasheado: `idf.py -p PORT storage-flash`
- Se deben revisar los logs del monitor para ver errores de SPIFFS

### No se puede conectar al WiFi
- Se debe verificar que el ESP32 esté encendido
- Se debe revisar que el SSID sea "ESP32_Server"
- Se debe asegurar de usar la contraseña correcta: "12345678"

### El servidor web no responde
- Se debe verificar en los logs que el servidor se haya iniciado
- Se debe asegurar de estar conectado a la red WiFi del ESP32
- Se debe acceder a `http://192.168.4.1`

### Sesión expirada constantemente
- Las sesiones expiran después de 3 minutos de inactividad
- Se debe volver a hacer login si la sesión expira

## Notas Técnicas

- **Sistema de Sesiones**: Basado en IP, máximo 5 sesiones simultáneas
- **Timeout de Sesión**: 3 minutos de inactividad
- **SPIFFS**: Sistema de archivos en flash, 1MB de capacidad
- **HTTP Server**: Puerto 80, soporta hasta 7 conexiones simultáneas
- **FreeRTOS**: El programa corre sobre FreeRTOS

## Licencia

Este proyecto es parte de un curso de RTOS.

## Autor

Proyecto desarrollado como parte del curso RTOS Raiz.

---

**Disfrute de su terminal retro.**
