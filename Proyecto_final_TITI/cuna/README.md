# Sistema RTOS para Control de Cuna Inteligente

**Versión:** 1.0.0  
**Autores:** Jair Hernan Telpis Cuaran, Luis Fernando Gamba Bedoya  
**Universidad:** Universidad Nacional de Colombia  
**Curso:** Sistemas Operativos en Tiempo Real (RTOS)

## Descripción

Sistema embebido basado en ESP32 con FreeRTOS que implementa control automático de ventilador mediante interfaz web, sensores y registros programados. El sistema utiliza arquitectura RTOS con tareas dedicadas, comunicación thread-safe y persistencia en memoria no volátil.

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
- **GET /dhtSensor.json**: Leer temperatura y sensor PIR
- Almacenamiento persistente en NVS (Non-Volatile Storage)

### Arquitectura RTOS
- **sensor_task**: Lectura periódica de sensores con mutex (2s)
- **fan_task**: Actualización automática del ventilador (1s)
- **keypad_task**: Polling del teclado matricial 4x4 (50ms)
- **display_task**: Gestión de pantalla OLED vía cola de mensajes
- **task_compare_hour**: Comparación de registros con hora actual

### Hardware Soportado
- Sensor NTC 10K (temperatura)
- Sensor PIR (movimiento)
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
├── pir_driver.c/h       → Driver sensor movimiento PIR
├── fan_driver.c/h       → Driver PWM ventilador
├── peripherals.c/h      → Gestión teclado y OLED
├── auth_display.c/h     → Sistema de autenticación local
└── board_config.h       → Configuración de pines GPIO
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
- `pir_driver.h/c` - Driver sensor movimiento PIR con ISR
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
