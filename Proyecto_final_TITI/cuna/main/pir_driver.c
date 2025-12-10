/**
 * @file pir_driver.c
 * @brief Implementación del driver PIR con soporte ISR y polling
 * @author Jair Hernan Telpis Cuaran, Luis Fernando Gamba Bedoya
 * @date 2025
 * 
 * @details
 * Implementa driver para sensor PIR HC-SR501 con dos modos de operación:
 * 
 * **Modo ISR (con cola):**
 * - Detecta flancos de subida y bajada automáticamente
 * - Envía eventos a cola FreeRTOS
 * - Ideal para respuesta inmediata
 * 
 * **Modo Polling (sin cola):**
 * - Lee estado bajo demanda con pir_is_motion_active()
 * - Más simple, usado por sensor_task
 * - Menor consumo de CPU
 * 
 * **Variables estáticas:**
 * - s_pir_gpio: Pin configurado para el sensor
 * - s_pir_evt_queue: Cola para eventos (NULL en modo polling)
 * 
 * @see pir_driver.h Para la API pública
 */

#include "pir_driver.h"
#include "driver/gpio.h"

/** @brief Pin GPIO configurado para el sensor PIR */
static gpio_num_t s_pir_gpio = GPIO_NUM_NC;

/** @brief Cola para eventos del PIR (NULL si se usa modo polling) */
static QueueHandle_t s_pir_evt_queue = NULL;

/**
 * @brief ISR (Interrupt Service Routine) del sensor PIR
 * 
 * @details
 * Se ejecuta automáticamente cuando el pin del PIR cambia de estado.
 * 
 * **Proceso:**
 * 1. Verifica que la cola esté configurada
 * 2. Lee nivel actual del GPIO
 * 3. Crea evento con estado del sensor
 * 4. Envía a cola desde ISR (xQueueSendFromISR)
 * 5. Yield si una tarea de mayor prioridad está esperando
 * 
 * **Características ISR:**
 * - Atributo IRAM_ATTR: Código en RAM para ejecución rápida
 * - Context: Interrupt context (no puede usar funciones bloqueantes)
 * - Tiempo: <10μs típico
 * 
 * @param[in] arg Parámetro de usuario (no usado)
 * 
 * @note Solo se ejecuta si se configuró cola en pir_init()
 * @warning No llamar directamente - ejecutada por hardware
 */
static void IRAM_ATTR pir_isr_handler(void *arg)
{
    if (s_pir_evt_queue == NULL) {
        return;
    }

    int level = gpio_get_level(s_pir_gpio);

    pir_event_t evt = {
        .motion = (level == 1)
    };

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(s_pir_evt_queue, &evt, &xHigherPriorityTaskWoken);

    if (xHigherPriorityTaskWoken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

/**
 * @brief Inicializa el sensor PIR configurando GPIO e ISR
 * @implements pir_init
 * 
 * @details
 * Secuencia de inicialización:
 * 1. Guarda parámetros en variables estáticas
 * 2. Configura GPIO como entrada sin pull-up/down
 * 3. Habilita interrupción en ambos flancos (ANYEDGE)
 * 4. Instala servicio ISR global (si no existe)
 * 5. Registra handler específico para este pin
 * 
 * **Configuración GPIO:**
 * - Modo INPUT: Lee nivel lógico del sensor
 * - Sin pull-up/down: Sensor tiene electrónica interna
 * - ANYEDGE: Detecta inicio (LOW→HIGH) y fin (HIGH→LOW)
 * 
 * **Alternativa solo subida:**
 * Cambiar a GPIO_INTR_POSEDGE para solo notificar inicio de movimiento.
 * 
 * @param[in] pir_gpio Pin GPIO (ej: GPIO_NUM_35)
 * @param[in] pir_queue Cola FreeRTOS o NULL para modo polling
 */
void pir_init(gpio_num_t pir_gpio, QueueHandle_t pir_queue)
{
    s_pir_gpio = pir_gpio;
    s_pir_evt_queue = pir_queue;

    // Configuración del GPIO como entrada con detección de flancos
    gpio_config_t io_conf = {
        .pin_bit_mask  = 1ULL << s_pir_gpio,       // Máscara de bits para el pin
        .mode          = GPIO_MODE_INPUT,          // Modo entrada
        .pull_up_en    = GPIO_PULLUP_DISABLE,      // Sin pull-up (sensor lo incluye)
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,    // Sin pull-down
        .intr_type     = GPIO_INTR_ANYEDGE         // Interrupción en ambos flancos
        // Alternativa: GPIO_INTR_POSEDGE para solo flanco de subida
    };

    gpio_config(&io_conf);

    // Instalar servicio global de ISR (solo primera vez)
    // Flags=0 usa valores por defecto
    gpio_install_isr_service(0);

    // Registrar ISR específica para este pin
    gpio_isr_handler_add(s_pir_gpio, pir_isr_handler, NULL);
}

/**
 * @brief Lee estado del sensor PIR mediante polling
 * @implements pir_is_motion_active
 * 
 * @details
 * Lee directamente el nivel del GPIO sin usar interrupciones.
 * 
 * **Proceso:**
 * 1. Verifica que el GPIO esté inicializado
 * 2. Lee nivel con gpio_get_level()
 * 3. Retorna true si nivel es HIGH (1)
 * 
 * **Seguridad:**
 * Si el sensor no se inicializó, retorna false para evitar lecturas erróneas.
 * 
 * @return bool Estado actual del sensor
 * @retval true Movimiento detectado (GPIO HIGH)
 * @retval false Sin movimiento o sensor no inicializado
 */
bool pir_is_motion_active(void)
{
    if (s_pir_gpio == GPIO_NUM_NC) {
        return false;
    }
    int level = gpio_get_level(s_pir_gpio);
    return (level == 1);
}
