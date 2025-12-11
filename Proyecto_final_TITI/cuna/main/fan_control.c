/**
 * @file fan_control.c
 * @brief Implementación del controlador multi-modo del ventilador
 * @author Jair Hernan Telpis Cuaran, Luis Fernando Gamba Bedoya
 * @date 2025
 * 
 * @details
 * Este módulo implementa la lógica de control del ventilador con 3 modos de operación:
 * 
 * **MODO MANUAL (FAN_MODE_MANUAL):**
 * - Usuario controla velocidad directamente desde web UI (0-100%)
 * - Velocidad PWM = velocidad del slider
 * - No aplica lógica automática
 * 
 * **MODO AUTOMÁTICO (FAN_MODE_AUTO):**
 * - Velocidad basada en temperatura del NTC
 * - Algoritmo: < 25°C → 0%, [25-30°C] → lineal, > 30°C → 100%
 * - Requiere sensor funcional (retorna 0% si error NTC)
 * - Actualizado por fan_task cada 1 segundo
 * 
 * **MODO REGISTROS (FAN_MODE_REGISTERS):**
 * - Activado por scheduler de registros programados
 * - Si hay registro activo: velocidad = slider
 * - Si no hay registro: ventilador apagado
 * - Actualizado cada 10s por registers_scheduler_task
 * 
 * **Arquitectura de actualización:**
 * @code
 *   [fan_task] ---(cada 1s)---> fan_control_update() si modo AUTO
 *        |
 *        v
 *   [compute_auto_speed()] ---> Lee NTC ---> Calcula PWM
 *        |
 *        v
 *   fan_set_speed_percent() ---> Actualiza hardware PWM
 * 
 *   [registers_scheduler] ---(cada 10s)---> fan_control_on_register_tick()
 *        |
 *        v
 *   Enciende/apaga según registros activos
 * @endcode
 * 
 * **Variables de estado:**
 * - s_mode: Modo actual (MANUAL/AUTO/REGISTERS)
 * - s_manual_speed: Velocidad del slider (0-100%)
 * - s_current_speed: Velocidad REAL del PWM (puede diferir del slider en AUTO)
 * - s_reg_active: Flag de registro programado activo
 * 
 * @see fan_control.h Para la API pública
 * @see fan_driver.h Para control de hardware PWM
 * @see registers.c Para scheduler de programación
 */

#include "fan_control.h"
#include "fan_driver.h"

#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "ntc_driver.h"
// #include "pir_driver.h"  // PIR desactivado para evitar conflictos con PWM

/**
 * @var s_ntc_adc_handle
 * @brief Handle externo del ADC para lectura de temperatura
 * 
 * @details
 * Definido en http_server.c e inicializado durante el arranque.
 * Usado por compute_auto_speed() para leer temperatura del NTC.
 * Si es NULL, modo AUTO retorna velocidad 0%.
 */
extern adc_oneshot_unit_handle_t s_ntc_adc_handle;

/** @brief Modo de operación actual del ventilador */
static fan_mode_t s_mode = FAN_MODE_MANUAL;

/** @brief Velocidad configurada por usuario en slider (0-100%) */
static uint8_t    s_manual_speed = 0;

/** 
 * @brief Velocidad REAL actual del ventilador PWM (0-100%)
 * 
 * @details
 * Puede diferir de s_manual_speed en modo AUTO.
 * - MANUAL: s_current_speed = s_manual_speed
 * - AUTO: s_current_speed = compute_auto_speed(temperatura)
 * - REGISTERS: s_current_speed = s_manual_speed si registro activo, 0 si no
 * 
 * Esta variable representa el duty cycle real del PWM.
 */
static uint8_t    s_current_speed = 0;

/** 
 * @brief Flag que indica si hay algún registro programado activo
 * 
 * @details
 * Actualizado por fan_control_on_register_tick() desde registers_scheduler_task.
 * Solo tiene efecto en modo FAN_MODE_REGISTERS.
 */
static bool       s_reg_active   = false;

/** 
 * @brief Temperatura mínima para activar el ventilador en modo AUTO (°C)
 * 
 * @details
 * Por debajo de este valor, el ventilador permanece apagado (0%).
 * Valor por defecto: 25.0°C.
 * Configurable mediante fan_control_set_temp_thresholds().
 */
static float      s_temp_min = 25.0f;

/** 
 * @brief Temperatura máxima para velocidad máxima en modo AUTO (°C)
 * 
 * @details
 * Por encima de este valor, el ventilador funciona a 100%.
 * Entre s_temp_min y s_temp_max, la velocidad es proporcional.
 * Valor por defecto: 30.0°C.
 * Configurable mediante fan_control_set_temp_thresholds().
 */
static float      s_temp_max = 30.0f;



/**
 * @brief Calcula velocidad del ventilador según temperatura en modo AUTO
 * 
 * @param[in] temp_c Temperatura leída del sensor NTC en °C
 * @return uint8_t Velocidad calculada (0-100%)
 * 
 * @details
 * Algoritmo de control proporcional:
 * - Si temp_c < s_temp_min: retorna 0% (ventilador apagado)
 * - Si temp_c >= s_temp_max: retorna 100% (velocidad máxima)
 * - Si temp_c entre [s_temp_min, s_temp_max]: interpolación lineal
 * 
 * **Fórmula de interpolación:**
 * \f[
 * velocidad = \frac{temp\_c - temp\_min}{temp\_max - temp\_min} \times 100\%
 * \f]
 * 
 * **Valores por defecto:**
 * - s_temp_min = 25.0°C
 * - s_temp_max = 30.0°C
 * 
 * **Ejemplo con valores por defecto:**
 * - 20°C → 0%
 * - 25°C → 0%
 * - 27.5°C → 50%
 * - 30°C → 100%
 * - 35°C → 100%
 * 
 * @note Los umbrales son configurables mediante fan_control_set_temp_thresholds()
 * @see fan_control_set_temp_thresholds() Para configurar los rangos
 */
static uint8_t compute_auto_speed(float temp_c)
{
    if (temp_c < s_temp_min) {
        return 0;
    }

    if (temp_c >= s_temp_max) {
        return 100;
    }

    // Mapeo lineal s_temp_min → 0%   s_temp_max → 100%
    float delta = s_temp_max - s_temp_min;
    if (delta <= 0.1f) {  // Protección contra división por cero
        return 100;
    }
    
    float scale = (temp_c - s_temp_min) / delta;  // 0 a 1
    return (uint8_t)(scale * 100.0f);
}

/**
 * @brief Inicializa el subsistema de control del ventilador
 * @implements fan_control_init
 * 
 * @details
 * Secuencia de inicialización:
 * 1. Inicializa hardware PWM (fan_init())
 * 2. Configura modo MANUAL por defecto
 * 3. Velocidades a 0%
 * 4. Apaga ventilador
 * 
 * **Estado inicial:**
 * - Modo: FAN_MODE_MANUAL
 * - Velocidad manual: 0%
 * - Velocidad actual: 0%
 * - Registro activo: false
 * - PWM: apagado
 * 
 * @warning Llamar antes de usar cualquier otra función de fan_control
 * @note Thread-safe: solo llamar una vez durante inicialización
 */
void fan_control_init(void)
{
    fan_init();
    s_mode = FAN_MODE_MANUAL;
    s_manual_speed = 0;
    s_current_speed = 0;
    s_reg_active = false;
    fan_off();
}

/**
 * @brief Cambia el modo de operación del ventilador
 * @implements fan_control_set_mode
 * 
 * @details
 * Actualiza el modo y aplica lógica correspondiente inmediatamente:
 * 
 * **MANUAL:**
 * - Aplica velocidad del slider directamente
 * - No requiere sensores
 * 
 * **AUTO:**
 * - Lee temperatura NTC
 * - Calcula velocidad con compute_auto_speed()
 * - Si ADC NULL o error NTC: apaga ventilador
 * - PIR deshabilitado en esta versión (comentado)
 * 
 * **REGISTERS:**
 * - Si hay registro activo: aplica velocidad del slider
 * - Si no hay registro: apaga ventilador
 * 
 * @param[in] mode Nuevo modo (FAN_MODE_MANUAL/AUTO/REGISTERS)
 * 
 * @warning No es thread-safe: llamar solo desde HTTP server task
 * @note Cambio de modo aplica velocidad inmediatamente (no espera a fan_task)
 * 
 * @see fan_control_update() Para actualizaciones periódicas en modo AUTO
 */
void fan_control_set_mode(fan_mode_t mode)
{
    s_mode = mode;

    switch (s_mode) {

    case FAN_MODE_MANUAL:
        // usa directamente la velocidad del slider
        s_current_speed = s_manual_speed;
        fan_set_speed_percent(s_current_speed);
        break;

    case FAN_MODE_AUTO: {
        // Al cambiar a AUTO, aplicamos una primera vez lógica NTC+PIR
        if (s_ntc_adc_handle == NULL) {
            s_current_speed = 0;
            fan_off();
            break;
        }

        float temp_c = leer_temperatura_celsius(s_ntc_adc_handle);
        if (temp_c < -100.0f) { // error NTC
            s_current_speed = 0;
            fan_off();
            break;
        }

        // PIR desactivado - asumir siempre movimiento
        // bool motion = pir_is_motion_active();
        // if (!motion) {
        //     s_current_speed = 0;
        //     fan_off();
        //     break;
        // }

        uint8_t spd = compute_auto_speed(temp_c);
        s_current_speed = spd;
        fan_set_speed_percent(s_current_speed);
        break;
    }

    case FAN_MODE_REGISTERS:
        if (s_reg_active) {
            s_current_speed = s_manual_speed;
            fan_set_speed_percent(s_current_speed);
        } else {
            s_current_speed = 0;
            fan_off();
        }
        break;
    }
}

/**
 * @brief Obtiene el modo de operación actual
 * @implements fan_control_get_mode
 * 
 * @return fan_mode_t Modo actual (MANUAL/AUTO/REGISTERS)
 * 
 * @note Thread-safe: lectura atómica de enum
 */
fan_mode_t fan_control_get_mode(void)
{
    return s_mode;
}

/**
 * @brief Establece la velocidad del slider (usado en MANUAL y REGISTERS)
 * @implements fan_control_set_manual_speed
 * 
 * @details
 * Actualiza s_manual_speed y aplica inmediatamente si está en modo MANUAL.
 * 
 * **Comportamiento por modo:**
 * - MANUAL: Actualiza PWM inmediatamente
 * - AUTO: Solo guarda valor (no afecta PWM)
 * - REGISTERS: Solo guarda valor (se aplica cuando registro activo)
 * 
 * @param[in] percent Velocidad deseada (0-100%)
 * 
 * @note Valores > 100 se limitan automáticamente a 100
 * @warning No es thread-safe: llamar solo desde HTTP server task
 */
void fan_control_set_manual_speed(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    s_manual_speed = percent;

    if (s_mode == FAN_MODE_MANUAL) {
        s_current_speed = s_manual_speed;
        fan_set_speed_percent(s_current_speed);
    }
}

/**
 * @brief Obtiene la velocidad REAL del ventilador PWM
 * @implements fan_control_get_speed
 * 
 * @details
 * Retorna s_current_speed, que es el duty cycle real del PWM.
 * Puede diferir de s_manual_speed en modo AUTO.
 * 
 * @return uint8_t Velocidad actual (0-100%)
 * 
 * @note Thread-safe: lectura atómica de uint8_t
 * @note Usado por OLED display y HTTP GET /api/dhtSensor
 */
uint8_t fan_control_get_speed(void)
{
    return s_current_speed;  // Devolver velocidad REAL del PWM, no del slider
}

/**
 * @brief Callback desde scheduler de registros programados
 * @implements fan_control_on_register_tick
 * 
 * @details
 * Llamado cada 10 segundos por registers_scheduler_task.
 * Solo tiene efecto si el modo actual es FAN_MODE_REGISTERS.
 * 
 * **Lógica:**
 * - Si any_match_now = true: Enciende ventilador a velocidad del slider
 * - Si any_match_now = false: Apaga ventilador
 * 
 * En otros modos (MANUAL/AUTO), esta función no hace nada.
 * 
 * @param[in] any_match_now true si hay al menos un registro activo ahora
 * 
 * @note Llamado desde registers_scheduler_task (diferente tarea)
 * @warning No es completamente thread-safe con cambios simultáneos de modo
 * 
 * @see registers.c Para el scheduler que llama esta función
 */
void fan_control_on_register_tick(bool any_match_now)
{
    s_reg_active = any_match_now;

    // Solo tiene efecto en modo REGISTROS
    if (s_mode != FAN_MODE_REGISTERS) {
        return;
    }

    if (s_reg_active) {
        // Aquí decides a qué velocidad se enciende cuando un registro coincide.
        // Yo uso la velocidad del slider (s_manual_speed).
        s_current_speed = s_manual_speed;
        fan_set_speed_percent(s_current_speed);
    } else {
        // Ningún registro activo → ventilador apagado
        s_current_speed = 0;
        fan_off();
    }
}

/**
 * @brief Establecer umbrales de temperatura para modo AUTO
 * @implements fan_control_set_temp_thresholds
 * 
 * @param[in] temp_min Temperatura mínima en °C (por debajo = ventilador apagado)
 * @param[in] temp_max Temperatura máxima en °C (por encima = velocidad 100%)
 * 
 * @details
 * Configura los rangos de temperatura para el algoritmo de control automático.
 * Entre temp_min y temp_max, la velocidad es proporcional lineal.
 * 
 * **Validaciones:**
 * - temp_min debe ser >= 0°C
 * - temp_max debe ser > temp_min (al menos 1°C de diferencia)
 * - Si temp_max <= temp_min: se mantienen valores anteriores
 * 
 * **Ejemplo de uso:**
 * @code
 * // Rango más amplio: 20-35°C
 * fan_control_set_temp_thresholds(20.0f, 35.0f);
 * 
 * // Rango estrecho: 27-29°C (respuesta más sensible)
 * fan_control_set_temp_thresholds(27.0f, 29.0f);
 * @endcode
 * 
 * @warning No es thread-safe: llamar solo desde HTTP server task
 * @note Los valores se aplican inmediatamente en próxima actualización de fan_control_update()
 */
void fan_control_set_temp_thresholds(float temp_min, float temp_max)
{
    // Validación básica
    if (temp_min < 0.0f || temp_max <= temp_min) {
        ESP_LOGW("fan_control", "Rangos de temperatura inválidos: min=%.1f max=%.1f (ignorados)",
                 temp_min, temp_max);
        return;
    }
    
    s_temp_min = temp_min;
    s_temp_max = temp_max;
    
    ESP_LOGI("fan_control", "Umbrales de temperatura actualizados: min=%.1f°C max=%.1f°C",
             s_temp_min, s_temp_max);
}

/**
 * @brief Obtener umbrales de temperatura configurados
 * @implements fan_control_get_temp_thresholds
 * 
 * @param[out] temp_min Puntero para almacenar temperatura mínima (puede ser NULL)
 * @param[out] temp_max Puntero para almacenar temperatura máxima (puede ser NULL)
 * 
 * @details
 * Retorna los valores actuales de los umbrales de temperatura.
 * Útil para mostrar configuración actual en interfaz web.
 * 
 * @note Thread-safe: lectura atómica de floats
 */
void fan_control_get_temp_thresholds(float *temp_min, float *temp_max)
{
    if (temp_min != NULL) {
        *temp_min = s_temp_min;
    }
    if (temp_max != NULL) {
        *temp_max = s_temp_max;
    }
}

/**
 * @brief Actualiza velocidad del ventilador en modo AUTO
 * @implements fan_control_update
 * 
 * @details
 * Llamado cada 1 segundo por fan_task.
 * Solo tiene efecto si el modo actual es FAN_MODE_AUTO.
 * 
 * **Algoritmo de actualización:**
 * 1. Verifica que el modo sea AUTO (si no, retorna)
 * 2. Verifica que el ADC esté inicializado
 * 3. Lee temperatura del NTC
 * 4. Valida lectura (> -100°C)
 * 5. Calcula velocidad con compute_auto_speed()
 * 6. Actualiza PWM con fan_set_speed_percent()
 * 
 * **Manejo de errores:**
 * - Si ADC handle es NULL: apaga ventilador
 * - Si temperatura < -100°C: apaga ventilador (error NTC)
 * - PIR deshabilitado: no verifica movimiento (comentado)
 * 
 * @note Llamado desde fan_task cada 1 segundo
 * @note Solo procesa en modo AUTO (retorna inmediatamente en otros modos)
 * @warning No es thread-safe con cambios de modo simultáneos
 * 
 * @see fan_task en http_server.c Para la tarea que llama esta función
 * @see compute_auto_speed() Para el algoritmo de cálculo
 */
void fan_control_update(void)
{
    if (s_mode != FAN_MODE_AUTO) {
        return;
    }

    if (s_ntc_adc_handle == NULL) {
        s_current_speed = 0;
        fan_off();
        return;
    }

    float temp_c = leer_temperatura_celsius(s_ntc_adc_handle);
    if (temp_c < -100.0f) { // error NTC
        s_current_speed = 0;
        fan_off();
        return;
    }

    // PIR desactivado
    // bool motion = pir_is_motion_active();
    // if (!motion) {
    //     s_current_speed = 0;
    //     fan_off();
    //     return;
    // }

    uint8_t auto_speed = compute_auto_speed(temp_c);
    s_current_speed = auto_speed;
    fan_set_speed_percent(s_current_speed);
}
