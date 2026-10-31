/* The mutexes for UART, I2C and SD (see locks.h). */
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "board.h"
#include "locks.h"

static SemaphoreHandle_t mutex[LOCK_COUNT];

void locks_init(void)
{
    uint8_t i;
    for (i = 0; i < LOCK_COUNT; i++) {
        mutex[i] = xSemaphoreCreateRecursiveMutex();
        configASSERT(mutex[i] != NULL);
    }
}

/* Before the scheduler starts only one piece of code runs, so nothing needs locking. */
int lock_take(lock_id_t id, uint32_t timeout_ms)
{
    TickType_t ticks;
    if (!rtos_running()) {
        return 1;
    }
    ticks = (timeout_ms == LOCK_FOREVER) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(mutex[id], ticks) == pdTRUE;
}

void lock_give(lock_id_t id)
{
    if (rtos_running()) {
        (void)xSemaphoreGiveRecursive(mutex[id]);
    }
}

int lock_held(lock_id_t id)
{
    if (!rtos_running()) {
        return 1;
    }
    return xSemaphoreGetMutexHolder(mutex[id]) == xTaskGetCurrentTaskHandle();
}
