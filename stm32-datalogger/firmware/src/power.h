/*
 * Low power between samples.
 *
 * FreeRTOS knows when the next task has to run. When nothing is ready it calls
 * power_idle(ms) from the idle task ("tickless idle"). Depending on the mode:
 *   POWER_RUN    return at once: the CPU spins in the idle task (the "before" measurement)
 *   POWER_SLEEP  WFI: the CPU clock stops, the peripherals and the 1 ms tick keep running
 *   POWER_STOP   STOP mode: all clocks stop except the 32 kHz RTC. The RTC alarm (EXTI 17)
 *                wakes the chip just before the next task is due; the time asleep is
 *                measured with the RTC and added to the FreeRTOS tick.
 *                A falling edge on PA10 (UART RX, EXTI 10) also wakes it: a key press.
 *                Both are EXTI events (WFE), so no interrupt handler is involved.
 *
 * Last fallback: the independent watchdog. It is fed every time the idle task runs (so also
 * after every wake-up). If the chip ever stayed asleep or stuck for about 20 s, the
 * watchdog restarts it, the start-up message says "Last reset: watchdog", and the board
 * then uses SLEEP instead of STOP until the next power-on ('power stop' turns STOP back on).
 *
 * STOP is used only when it is safe, otherwise SLEEP (the fallback):
 *   - the RTC crystal runs (it is the only clock that can wake the chip),
 *   - no debugger is attached (it would lose the connection),
 *   - more than 10 s since reset (time to start flashing), no key in the last 30 s
 *     (the UART cannot receive in STOP), and the UART has finished sending,
 *   - the idle time is at least 10 ms.
 */
#ifndef POWER_H
#define POWER_H

#include <stdint.h>

typedef enum { POWER_RUN, POWER_SLEEP, POWER_STOP } power_mode_t;

void         power_init(void);              /* EXTI lines, interrupts, watchdog; before the scheduler */
void         power_set_mode(power_mode_t m);
power_mode_t power_mode(void);
const char  *power_mode_name(power_mode_t m);
int          power_parse_mode(const char *s, power_mode_t *m);   /* "run", "sleep", "stop" */

void         power_idle(uint32_t expected_ms);   /* FreeRTOS calls this (portSUPPRESS_TICKS_AND_SLEEP) */
int          power_uart_byte_ok(void);           /* UART ISR: 0 = byte is the garbage of a wake-up */
void         power_console_activity(void);       /* UART ISR: a key, stay awake for a while */

uint32_t     power_stop_count(void);             /* times the chip was in STOP */
uint32_t     power_stop_ms(void);                /* total time in STOP */
uint32_t     power_key_wakes(void);              /* wake-ups by a key (PA10) */
int32_t      power_stop_test(void);              /* one 1 s STOP; RTC counts slept, -1 = failed */
uint32_t     power_stats_ms(void);               /* ms since the counters were cleared (or reset) */
void         power_stats_reset(void);            /* 'power reset': clear the STOP counters */

#endif
