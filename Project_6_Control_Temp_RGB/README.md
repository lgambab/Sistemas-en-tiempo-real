# Proyecto 5: Potenciómetro con Sensor de Temperatura y LED RGB

Este proyecto implementa un sistema RTOS (Real-Time Operating System) en ESP32 que controla LEDs RGB basándose en las lecturas de un potenciómetro y un sensor de temperatura NTC.

## Descripción del Sistema

El sistema utiliza FreeRTOS para crear múltiples tareas que funcionan de manera concurrente:

- **Potenciómetro**: Controla el brillo del LED verde (GPIO27)
- **Sensor NTC**: Controla el brillo del LED rojo (GPIO25) basándose en la temperatura
- **Monitor Serie**: Muestra información en tiempo real del sistema

## Hardware Requerido

- **ESP32** (cualquier variante compatible)
- **Potenciómetro 10kΩ** conectado a GPIO34 (ADC1_CH6)
- **Sensor NTC 10kΩ** conectado a GPIO26 (ADC2_CH9)
- **LED Verde** en GPIO27
- **LED Rojo** en GPIO25
- **Resistencia de 10kΩ** en serie con el sensor NTC

## Flujo del Código

### 1. Inicialización del Sistema (`main.c`)

```c
void app_main(void)
{
    // 1. Inicialización de hardware
    pot_init();           // ADC1 para potenciómetro
    rgb_led_init();       // PWM para LED verde
    ntc_sensor_init();    // ADC2 para sensor NTC
    ntc_led_pwm_init();   // PWM para LED rojo
    
    // 2. Creación de colas de comunicación
    pot_queue = xQueueCreate(5, sizeof(pot_data_t));
    ntc_queue = xQueueCreate(5, sizeof(ntc_data_t));
    
    // 3. Creación de tareas RTOS
    xTaskCreate(pot_reading_task, ...);      // Lectura potenciómetro
    xTaskCreate(ntc_reading_task, ...);      // Lectura sensor NTC
    xTaskCreate(rgb_control_task, ...);      // Control LED verde
    xTaskCreate(ntc_led_control_task, ...);  // Control LED rojo
    xTaskCreate(display_info_task, ...);     // Monitor serie
}
```

### 2. Tareas del Sistema

#### **Tarea de Lectura del Potenciómetro** (`pot_reading_task`)
- **Frecuencia**: 4 veces por segundo (250ms)
- **Función**: Lee el valor del ADC1 y envía datos a la cola
- **Datos**: Porcentaje (0-100%) y voltaje (0-3300mV)

#### **Tarea de Lectura del Sensor NTC** (`ntc_reading_task`)
- **Frecuencia**: Cada 2 segundos
- **Función**: Lee temperatura, calcula resistencia y brillo del LED
- **Cálculos**: Ecuación de Steinhart-Hart para conversión temperatura

#### **Tarea de Control LED Verde** (`rgb_control_task`)
- **Función**: Recibe datos del potenciómetro y ajusta brillo del LED verde
- **Mapeo**: 0-100% del potenciómetro → 0-100% brillo LED

#### **Tarea de Control LED Rojo** (`ntc_led_control_task`)
- **Función**: Recibe datos del sensor NTC y ajusta brillo del LED rojo
- **Mapeo**: 10-50°C → 0-100% brillo LED

#### **Tarea de Visualización** (`display_info_task`)
- **Frecuencia**: Cada 1 segundo
- **Función**: Muestra información del sistema por puerto serie

## Estructura de Archivos en `/main`

### `main.c`
**Archivo principal del sistema**
- Contiene la función `app_main()` que inicializa todo el sistema
- Define las 5 tareas RTOS del sistema
- Gestiona las colas de comunicación entre tareas
- Implementa el monitoreo en tiempo real

### `potentiometer.c` / `potentiometer.h`
**Módulo del potenciómetro**
- **Hardware**: ADC1_CH6 (GPIO34)
- **Funciones principales**:
  - `pot_init()`: Inicializa ADC1 con calibración
  - `pot_get_voltage_mv()`: Retorna voltaje en milivoltios
  - `pot_get_percent()`: Retorna porcentaje (0-100%)
- **Características**: Promedio de 8 muestras para mayor precisión

### `ntc_sensor.c` / `ntc_sensor.h`
**Módulo del sensor de temperatura NTC**
- **Hardware**: ADC2_CH9 (GPIO26) + LED rojo (GPIO25)
- **Funciones principales**:
  - `ntc_sensor_init()`: Inicializa ADC2
  - `ntc_led_pwm_init()`: Configura PWM para LED rojo
  - `ntc_read_temperature()`: Lee y calcula temperatura
  - `ntc_update_led_brightness()`: Controla brillo del LED
- **Cálculos**: Ecuación de Steinhart-Hart para conversión temperatura
- **Rango**: 10-50°C mapeado a 0-100% brillo

### `rgb_led.c` / `rgb_led.h`
**Módulo del LED RGB (canal verde)**
- **Hardware**: LED verde (GPIO27)
- **Funciones principales**:
  - `rgb_led_init()`: Configura PWM para LED verde
  - `rgb_set_green_percent()`: Establece brillo (0-100%)
- **Características**: PWM de 8 bits, 5kHz

## Configuración de Hardware

```
ESP32 Pinout:
├── GPIO34 (ADC1_CH6) → Potenciómetro (centro)
├── GPIO26 (ADC2_CH9) → Sensor NTC (centro)
├── GPIO27 → LED Verde (ánodo)
├── GPIO25 → LED Rojo (ánodo)
├── 3.3V → Potenciómetro y NTC (extremos)
└── GND → LEDs (cátodo) y resistencias
```

## Frecuencias de Operación

- **Lectura potenciómetro**: 4 Hz (250ms)
- **Lectura sensor NTC**: 0.5 Hz (2000ms)
- **Monitor serie**: 1 Hz (1000ms)
- **PWM LEDs**: 5 kHz

## Compilación y Flasheo

```bash
# Configurar el proyecto
idf.py set-target esp32

# Compilar
idf.py build

# Flashear y monitorear
idf.py -p PORT flash monitor
```

## Monitoreo del Sistema

El sistema muestra información en tiempo real:

```
=== SISTEMA DE MONITOREO ===
LED Verde: 45% | Potenciómetro: 1485 mV
Temperatura: 23.5°C | LED Rojo: 45.0% brillo
=============================
```

## Características Técnicas

- **RTOS**: FreeRTOS con 5 tareas concurrentes
- **Comunicación**: Colas de mensajes entre tareas
- **ADC**: Calibración automática (curve/line fitting)
- **PWM**: Resolución 8-bit (LED verde) y 10-bit (LED rojo)
- **Precisión**: Promedio de múltiples muestras ADC
- **Tolerancia**: Manejo de errores y logging detallado
