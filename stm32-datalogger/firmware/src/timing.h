/*
 * Time measurements in microseconds. The clock is the FreeRTOS tick (1 ms) plus the SysTick
 * counter inside the current millisecond (8 counts per microsecond). It keeps counting
 * through STOP mode, because power.c adds the time asleep (measured by the RTC) to the tick.
 * (The DWT cycle counter stops in STOP; it is used only for the CPU numbers of 'tasks',
 * which therefore mean "share of the time the CPU was awake".)
 * Each measurement keeps the last, smallest and largest value and how many were taken.
 * The 'timing' command prints them. PB12 (debug pin) is high while the sensors are read,
 * so a logic analyzer shows the same thing from outside.
 */
#ifndef TIMING_H
#define TIMING_H

#include <stdint.h>

typedef enum {
    TM_PERIOD,      /* time between the starts of two samples (shows jitter) */
    TM_SENSOR,      /* reading BMP280 + MPU6050 + ADC (mostly waiting for the BMP280) */
    TM_FORMAT,      /* building the CSV line and its CRC */
    TM_UART,        /* handing the line to the UART (DMA does the sending) */
    TM_SD,          /* writing the line to the SD card (longer when it syncs) */
    TM_LATENCY,     /* from the start of the sample until the line is on the card */
    TM_COUNT
} tm_id_t;

typedef struct {
    uint32_t last, min, max, count;     /* microseconds */
} tm_stat_t;

uint32_t    timing_now_us(void);                    /* microseconds since start (wraps after 71 min) */
void        timing_record(tm_id_t id, uint32_t us);
void        timing_get(tm_id_t id, tm_stat_t *out);
void        timing_reset(void);
const char *timing_name(tm_id_t id);
uint64_t    timing_runtime_counter(void);           /* 64-bit cycle count, for FreeRTOS run-time stats */

#endif
