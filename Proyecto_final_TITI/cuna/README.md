# Sistema RTOS para Control de Cuna Inteligente

**Versión:** 1.0.0  
**Autores:** Jair Hernan Telpis Cuaran, Luis Fernando Gamba Bedoya  
**Universidad:** Universidad Nacional de Colombia  
**Curso:** Sistemas Operativos en Tiempo Real (RTOS)

## Descripción

Sistema embebido basado en ESP32 con FreeRTOS que implementa control automático de ventilador mediante interfaz web, sensores y registros programados. El sistema utiliza arquitectura RTOS con tareas dedicadas, comunicación thread-safe y persistencia en memoria no volátil.

## Arquitectura del ESP32

### Hardware
- **Procesador**: Xtensa® Dual-Core 32-bit LX6
- **Frecuencia**: Hasta 240 MHz (ajustable por software)
- **Memoria**:
  - **SRAM**: 520 KB
  - **Flash externa**: 4 MB (típico, configurado en particiones)
  - **ROM**: 448 KB (bootloader y librerías)
- **Conectividad**:
  - **WiFi**: 802.11 b/g/n (2.4 GHz) con modos AP/STA/AP+STA
  - **Bluetooth**: v4.2 BR/EDR y BLE (no utilizado en este proyecto)
- **Periféricos integrados**:
  - 34× GPIO configurables (INPUT/OUTPUT/LEDC/ADC/I2C/etc.)
  - 2× ADC de 12-bit (18 canales)
  - 2× DAC de 8-bit
  - 16× canales PWM (LEDC)
  - 4× SPI, 2× I2C, 3× UART
  - Timers, RTC, watchdogs, capacitive touch

### Sistema Operativo
- **FreeRTOS**: v10.5.1 integrado en ESP-IDF v5.5.1
- **Multitarea cooperativa**: Scheduler con tick de 1 ms (100 Hz configurable)
- **Arquitectura simétrica (SMP)**: Tareas distribuidas en 2 núcleos
  - **Core 0**: Tareas del sistema WiFi/BT (PRO_CPU)
  - **Core 1**: Tareas de aplicación (APP_CPU, por defecto)
- **Stack dinámico**: Heap de 200+ KB disponible tras boot
- **Sincronización**: Mutex, semáforos, colas, grupos de eventos

## Características Principales

### Control del Ventilador (PWM)
- **Modo Manual**: Control directo mediante slider en interfaz web
- **Modo Automático**: Ajuste basado en temperatura del sensor NTC
  - < 25°C → Apagado
  - 25-30°C → Velocidad proporcional
  - > 30°C → Velocidad máxima
- **Modo Registros**: Activación programada por horarios

### API REST
- **POST /api/register**: Crear/actualizar registro programado
- **GET /api/register**: Listar todos los registros
- **DELETE /api/register?id=X**: Eliminar registro
- **GET /dhtSensor.json**: Leer temperatura
- Almacenamiento persistente en NVS (Non-Volatile Storage)

### Arquitectura RTOS
- **sensor_task**: Lectura periódica de sensores con mutex (2s)
- **fan_task**: Actualización automática del ventilador (1s)
- **keypad_task**: Polling del teclado matricial 4x4 (50ms)
- **display_task**: Gestión de pantalla OLED vía cola de mensajes
- **task_compare_hour**: Comparación de registros con hora actual

### Hardware Soportado
- Sensor NTC 10K (temperatura)
- Ventilador DC con control PWM
- Pantalla OLED SSD1306 128x64 I2C
- Teclado matricial 4x4
- WiFi AP/STA para servidor web

## Estructura del Proyecto

```
main/
├── http_server.c/h      → Servidor HTTP, endpoints REST
├── registers.c/h        → API de registros (CRUD + NVS)
├── fan_control.c/h      → Lógica de control del ventilador
├── sensor_task.c/h      → Tarea dedicada para sensores
├── wifi_app.c/h         → Gestión WiFi AP/STA
├── ntc_driver.c/h       → Driver sensor temperatura NTC
├── fan_driver.c/h       → Driver PWM ventilador
├── peripherals.c/h      → Gestión teclado y OLED
├── auth_display.c/h     → Sistema de autenticación local
└── board_config.h       → Configuración de pines GPIO
```

## Periféricos Utilizados

### Sensores
- **NTC 10K Ohm** - Termistor para medición de temperatura
  - Interface: ADC (ADC1_CHANNEL_6, GPIO 34)
  - Modo GPIO: Analógico (ADC oneshot)
  - Resolución: 12 bits
  - Conversión: Ecuación Steinhart-Hart
  - Precisión: 0.1°C

### Actuadores
- **Ventilador DC**
  - Control: PWM (Pulse Width Modulation)
  - Modo GPIO: LEDC Output
  - Canal LEDC: Canal 0
  - Frecuencia: 5000 Hz
  - Resolución: 8 bits (0-255)
  - GPIO: Configurable en board_config.h

### Interfaces de Usuario
- **Pantalla OLED SSD1306**
  - Resolución: 128x64 píxeles
  - Protocolo: I2C
  - Dirección: 0x3C
  - SDA (GPIO 4): Modo OUTPUT/INPUT (I2C bidireccional)
  - SCL (GPIO 15): Modo OUTPUT (I2C clock)
  - Pull-up: Resistencias externas requeridas (4.7kΩ)
  - Uso: Autenticación y visualización de datos
  
- **Teclado Matricial 4x4**
  - Filas (GPIO 12, 13, 14, 27): Modo OUTPUT (GPIO_MODE_OUTPUT)
    - Estado inactivo: LOW
    - Escaneo: HIGH secuencial por fila
  - Columnas (GPIO 32, 33, 25, 26): Modo INPUT (GPIO_MODE_INPUT)
    - Pull-down: GPIO_PULLDOWN_ONLY
    - Lectura: Detecta HIGH cuando tecla presionada
  - Polling: 50ms
  - Debounce: 80-100ms por software
  - Uso: Ingreso de contraseña y navegación

### LED RGB (Indicador de Estado)
- R (GPIO 0): Modo LEDC_OUTPUT (LEDC_CHANNEL_0)
- G (GPIO 4): Modo LEDC_OUTPUT (LEDC_CHANNEL_1)
- B (GPIO 2): Modo LEDC_OUTPUT (LEDC_CHANNEL_2)
- PWM: 100 Hz, 8 bits de resolución
- Timer: LEDC_TIMER_0, LEDC_LOW_SPEED_MODE
- Estados:
  - Cian: Servidor HTTP activo
  - Verde: WiFi conectado
  - Azul: WiFi iniciando
  - Rojo: Error OTA

### LEDs Indicadores
- **LED Blink** (GPIO 5): Modo OUTPUT (GPIO_MODE_OUTPUT)
  - Uso: Debug y toggle desde web
  
- **LED Info Display** (GPIO 22): Modo OUTPUT (GPIO_MODE_OUTPUT)
  - Uso: Indicador de información activa en OLED

## Protocolos de Comunicación

### Conectividad WiFi
- **Modo Access Point (AP)**
  - SSID: ESP32_AP
  - Seguridad: WPA2-PSK
  - DHCP Server: 192.168.0.1/24
  - Canales: 1-13
  - Máximo clientes: 4

- **Modo Station (STA)**
  - Soporte WPA/WPA2/WPA3
  - Reconexión automática
  - Persistencia de credenciales en NVS
  - DHCP Client activo

### Protocolo HTTP/1.1
- **Servidor Web ESP32**
  - Puerto: 80
  - Stack por conexión: 4096 bytes
  - Conexiones simultáneas: CONFIG_LWIP_MAX_SOCKETS
  - Keep-alive: Configurable por endpoint
  
- **Métodos Soportados**
  - GET: Lectura de datos (sensores, registros, estado)
  - POST: Escritura de datos (registros, configuración WiFi)
  - DELETE: Eliminación de registros

### Protocolo SNTP
- **Sincronización de Tiempo**
  - Servidor: pool.ntp.org
  - Intervalo de sincronización: Configurable
  - Zona horaria: UTC-5 (Colombia)
  - Uso: Activación de registros programados

### Protocolo I2C
- **OLED SSD1306**
  - Velocidad: 400 kHz (Fast Mode)
  - Modo: Master
  - Timeout: 1000ms

## Formato de Comunicación JSON

### Estructura de Mensajes REST

**1. Lectura de Sensores (GET /dhtSensor.json)**
```json
{
  "temp": 25.3,

  "display": 1
}
```

**2. Control de Ventilador (POST /fanControl.json)**
```json
{
  "mode": "auto",
  "speed": 75
}
```

**3. Registro Programado (POST /api/register)**
```json
{
  "id": 1,
  "hour": 22,
  "minute": 30,
  "days": [1, 1, 1, 1, 1, 0, 0],
  "action": "fan_on",
  "speed": 50
}
```

**4. Configuración WiFi (POST /wifiConnect.json)**
```json
{
  "selectedSSID": "MiRed",
  "pwd": "contraseña123"
}
```

**5. Estado OTA (GET /OTAstatus)**
```json
{
  "ota_update_status": 1,
  "compile_time": "14:30:45",
  "compile_date": "Dec 11 2025"
}
```

### Parseo y Generación JSON
- **Backend**: Librería cJSON (espressif__cjson)
- **Frontend**: JSON nativo de JavaScript
- **Validación**: Verificación de campos obligatorios
- **Content-Type**: application/json

## Uso de FreeRTOS

### Tareas Principales

**sensor_task (Prioridad 5)**
```c
- Stack: 4096 bytes
- Periodo: 2000ms
- Core: 1
- Función: Lectura thread-safe de NTC
- Sincronización: Mutex para protección de datos compartidos
```

**fan_task (Prioridad configMAX_PRIORITIES-4)**
```c
- Stack: 2048 bytes
- Periodo: 1000ms
- Función: Actualización automática del ventilador en modo AUTO
- Llamadas: fan_control_update()
```

**wifi_app_task (Prioridad 5)**
```c
- Stack: 4096 bytes
- Función: Gestión de eventos WiFi y mensajes
- Cola: wifi_app_queue_handle (3 mensajes)
- Mensajes: START_HTTP_SERVER, CONNECTING, STA_CONNECTED_GOT_IP
```

**http_server_monitor (Prioridad 4)**
```c
- Stack: 3072 bytes
- Función: Monitor de estados WiFi para servidor HTTP
- Cola: http_server_monitor_queue_handle (3 mensajes)
- Sincronización: Notificaciones entre wifi_app y http_server
```

**registers_scheduler_task (Prioridad 3)**
```c
- Stack: 4096 bytes
- Periodo: 10000ms (10 segundos)
- Función: Verificación de registros programados
- Dependencia: Requiere SNTP sincronizado
```

**check_sta_connection_state (Prioridad 5)**
```c
- Stack: 4096 bytes
- Periodo: 30000ms
- Función: Monitoreo y reconexión WiFi automática
- Reintentos: Hasta WIFI_RETRY_ATTEMPTS
```

**keypad_task (Prioridad 3)**
```c
- Stack: 2048 bytes
- Periodo: 50ms
- Función: Polling del teclado matricial 4x4
- Debounce: Filtrado por software
```

**display_refresh_task (Prioridad 3)**
```c
- Stack: 2048 bytes
- Periodo: 1000ms
- Función: Actualización de información en OLED
- Dependencia: Autenticación exitosa
```

### Mecanismos de Sincronización

**Mutex (Exclusión Mutua)**
```c
- sensor_data_mutex: Protege datos de sensor_task
- Operaciones: xSemaphoreTake() / xSemaphoreGive()
- Timeout: portMAX_DELAY (espera infinita)
- Uso: Lectura thread-safe desde múltiples contextos
```

**Colas de Mensajes**
```c
- wifi_app_queue_handle: Comunicación eventos WiFi
- http_server_monitor_queue_handle: Estados HTTP/WiFi
- Tamaño: 3 mensajes
- Timeout: portMAX_DELAY o timeouts específicos
```

**Notificaciones de Tarea**
```c
- RGB LED: Notificaciones entre tareas para cambios de estado
- Ventajas: Menor uso de RAM que colas
- API: xTaskNotify() / xTaskNotifyWait()
```

### Gestión de Memoria
- **Heap FreeRTOS**: Asignación dinámica para tareas
- **Stack por tarea**: Tamaños específicos según complejidad
- **Heap tracking**: Monitoreo de memoria disponible
- **Stack overflow detection**: CONFIG_FREERTOS_CHECK_STACKOVERFLOW

### Temporización
- **vTaskDelay()**: Delays no bloqueantes (libera CPU)
- **pdMS_TO_TICKS()**: Conversión milisegundos a ticks
- **Tick rate**: 100 Hz (CONFIG_FREERTOS_HZ)
- **Timers por software**: ESP Timer para OTA reset (8s)

### Prioridades de Tareas
```
0: Idle Task (sistema)
3: Baja (keypad, display, registers_scheduler)
4: Media (http_server_monitor)
5: Alta (sensor_task, wifi_app_task, check_sta_connection)
```

### Event Groups
```c
- WiFi events: WIFI_EVENT, IP_EVENT
- Event handlers: wifi_app_event_handler()
- Bits de estado: Conexión, desconexión, IP asignada
```

## Compilación

```bash
# Configurar ESP-IDF v5.5.1
. $HOME/esp/esp-idf/export.sh

# Compilar
idf.py build

# Flashear
idf.py -p COM3 flash monitor
```

## Documentación Doxygen

### Archivos Documentados:

**Backend (C/ESP-IDF):**
- `sensor_task.h/c` - Tarea dedicada de sensores con mutex
- `fan_control.h/c` - Control del ventilador (manual/auto/registros)
- `registers.h/c` - API REST de registros programados
- `http_server.h/c` - Servidor HTTP con endpoints REST completos
- `wifi_app.h/c` - Gestión WiFi AP/STA dual con reconexión y SNTP
- `ntc_driver.h/c` - Driver sensor temperatura NTC con Steinhart-Hart
- `fan_driver.h/c` - Driver PWM del ventilador

**Frontend (JavaScript):**
- `app.js` - Aplicación web completa
  - Control de ventilador (3 modos)
  - Gestión de registros (CRUD)
  - Monitoreo de sensores
  - Configuración WiFi
  - Actualización OTA

### Generar documentación HTML:

**En Windows:**
```powershell
# Instalar Doxygen desde: https://www.doxygen.nl/download.html
# O con Chocolatey:
choco install doxygen.install

# Generar documentación
cd C:\Users\lgamb\Documents\Uni\RTOS\Repositorio\RTOS_Repo_Limpio\Proyecto_final_TITI\cuna
doxygen Doxyfile

# Abrir en navegador
start docs\html\index.html
```

**En Linux:**
```bash
# Instalar doxygen
sudo apt install doxygen

# Generar docs
doxygen Doxyfile

# Abrir en navegador
xdg-open docs/html/index.html
```

La documentación incluye:
- Descripción detallada de funciones y estructuras
- Diagramas de llamadas y dependencias
- Endpoints REST disponibles (C y JavaScript)
- Parámetros, valores de retorno y excepciones
- Ejemplos de uso y notas importantes
- Navegación unificada entre backend y frontend

## Configuración WiFi

El sistema crea un Access Point por defecto:
- **SSID**: ESP32_AP
- **Password**: (configurar en código)
- **IP**: 192.168.0.1

También puede conectarse a redes WiFi existentes mediante la interfaz web.

## Sincronización de Hora

El sistema usa SNTP para sincronizar hora con servidores NTP:
- Servidor: pool.ntp.org
- Zona horaria: Configurable
- Requerido para funcionamiento de registros programados

## Thread Safety

- **sensor_task**: Datos protegidos por mutex FreeRTOS
- **display_task**: Comunicación mediante cola de mensajes
- **fan_control**: Variables estáticas protegidas implícitamente por ejecución secuencial

## Licencia

Proyecto académico - Universidad Nacional de Colombia
