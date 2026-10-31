/*
 * Real time clock on the 32.768 kHz crystal, seconds since 1970-01-01.
 * Keeps time through a reset. Without a battery on VBAT it starts again
 * from 1970 after the power was off.
 * The counter runs at 1024 Hz; power.c uses it for the STOP-mode alarm.
 */
#ifndef RTC_H
#define RTC_H

#include <stdint.h>

#define RTC_MIN_VALID   1704067200U     /* 2024-01-01: anything earlier means 'not set' */

int      rtc_needs_setup(void);         /* 1 = the backup area lost its settings (first start) */
int      rtc_init(void);                /* 1 = clock running, 0 = crystal did not start */
uint32_t rtc_start_ms(void);            /* ms the crystal needed to start at this power-up */
int      rtc_running(void);              /* 0 = no crystal: rtc_get/rtc_set use a software clock */
int      rtc_clock_is_set(void);        /* running and later than 2024-01-01 */
uint32_t rtc_get(void);
void     rtc_set(uint32_t secs);

#define RTC_HZ 1024U                    /* counter ticks per second */
uint32_t rtc_counter(void);             /* the raw 1024 Hz counter */
int      rtc_set_alarm(uint32_t count); /* alarm (EXTI line 17) when the counter reaches count */
void     rtc_clear_alarm(void);
void     rtc_resync(void);              /* after STOP: wait until the registers are valid again */

/* calendar maths: days since 1970-01-01 <-> year, month, day */
uint32_t days_from_date(int32_t y, int32_t m, int32_t d);
void     date_from_days(uint32_t z, int32_t *y, int32_t *m, int32_t *d);
/* text must look exactly like 2026-10-16 20:30:00 and be a real date. 1 = ok. */
int      parse_datetime(const char *s, uint32_t *secs);

#endif
