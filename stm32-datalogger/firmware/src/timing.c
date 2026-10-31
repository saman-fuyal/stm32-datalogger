/* DWT cycle counter measurements and the counter for FreeRTOS run-time statistics. */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
#include "timing.h"

static tm_stat_t stats[TM_COUNT];

static const char *const names[TM_COUNT] = {
    "sample period", "sensor read", "line + CRC", "UART (DMA)", "SD write", "sample -> SD"
};

uint32_t timing_now_us(void)
{
    TickType_t t1, t2;
    uint32_t val;
    do {                                    /* read again if a tick happened in between */
        t1 = xTaskGetTickCount();
        val = SYST_CVR;                     /* counts down from 7999 to 0 in each ms */
        t2 = xTaskGetTickCount();
    } while (t1 != t2);
    return (uint32_t)t1 * 1000U + ((SYST_RVR - val) / (CPU_HZ / 1000000U));
}

void timing_record(tm_id_t id, uint32_t us)
{
    tm_stat_t *s = &stats[id];
    taskENTER_CRITICAL();                   /* two tasks record; keep the four values together */
    s->last = us;
    if (s->count == 0U || us < s->min) { s->min = us; }
    if (us > s->max) { s->max = us; }
    s->count++;
    taskEXIT_CRITICAL();
}

void timing_get(tm_id_t id, tm_stat_t *out)
{
    taskENTER_CRITICAL();
    *out = stats[id];
    taskEXIT_CRITICAL();
}

void timing_reset(void)
{
    uint8_t i;
    taskENTER_CRITICAL();
    for (i = 0; i < TM_COUNT; i++) {
        stats[i].last = stats[i].min = stats[i].max = stats[i].count = 0;
    }
    taskEXIT_CRITICAL();
}

const char *timing_name(tm_id_t id)
{
    return names[id];
}

/* The 32-bit counter wraps every 537 s at 8 MHz. FreeRTOS reads this at every task switch
   (at least once per 1 ms tick), so a wrap is always seen and counted in 'high'.
   Interrupts are switched off for these few instructions because it is called both from
   tasks and from the task-switch interrupt. */
uint64_t timing_runtime_counter(void)
{
    static uint32_t last = 0, high = 0;
    uint32_t primask, now;

    __asm volatile ("mrs %0, primask\n cpsid i" : "=r" (primask) :: "memory");
    now = DWT_CYCCNT;
    if (now < last) {
        high++;
    }
    last = now;
    __asm volatile ("msr primask, %0" :: "r" (primask) : "memory");
    return ((uint64_t)high << 32) | now;
}
