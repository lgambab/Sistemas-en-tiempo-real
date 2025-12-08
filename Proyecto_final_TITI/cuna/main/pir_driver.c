#include "pir_driver.h"
#include "driver/gpio.h"

static gpio_num_t s_pir_gpio = GPIO_NUM_NC;
static QueueHandle_t s_pir_evt_queue = NULL;

/**
 * ISR del PIR: se dispara en flanco (subida/bajada) y manda evento a la cola.
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

void pir_init(gpio_num_t pir_gpio, QueueHandle_t pir_queue)
{
    s_pir_gpio = pir_gpio;
    s_pir_evt_queue = pir_queue;

    // Configuración del pin como entrada
    gpio_config_t io_conf = {
        .pin_bit_mask  = 1ULL << s_pir_gpio,
        .mode          = GPIO_MODE_INPUT,
        .pull_up_en    = GPIO_PULLUP_DISABLE,
        .pull_down_en  = GPIO_PULLDOWN_DISABLE, // muchos módulos PIR ya traen electrónica interna
        .intr_type     = GPIO_INTR_ANYEDGE      // flanco de subida y bajada
        // Si SOLO quieres notificar cuando hay movimiento:
        // .intr_type  = GPIO_INTR_POSEDGE;
    };

    gpio_config(&io_conf);

    // Instalar servicio de ISR (si aún no se ha instalado, puedes usar 0)
    gpio_install_isr_service(0);

    // Registrar ISR para este pin
    gpio_isr_handler_add(s_pir_gpio, pir_isr_handler, NULL);
}

bool pir_is_motion_active(void)
{
    if (s_pir_gpio == GPIO_NUM_NC) {
        return false;
    }
    int level = gpio_get_level(s_pir_gpio);
    return (level == 1);
}
