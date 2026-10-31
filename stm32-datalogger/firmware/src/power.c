/* STOP mode in the FreeRTOS idle task, woken by the RTC alarm or a key (see power.h). */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
#include "config.h"
#include "fmt.h"
#include "power.h"
#include "rtc.h"
#include "uart.h"

#define TICK_RELOAD       ((configCPU_CLOCK_HZ / configTICK_RATE_HZ) - 1U)
#define WAKE_GARBAGE_MS   3U              /* bytes this soon after a key wake-up are broken */

static volatile power_mode_t mode = POWER_MODE_DEFAULT;
static volatile TickType_t   console_until = 0;
static volatile TickType_t   key_wake_tick = 0;
static volatile uint8_t      key_wake_recent = 0;
static uint32_t stop_count = 0;
static uint32_t stop_rtc_total = 0;       /* in RTC counts (1/1024 s) */
static uint32_t key_wakes = 0;
static uint32_t carry = 0;                /* part of a millisecond not yet given to the tick */
static TickType_t stats_since = 0;        /* tick when the counters were last cleared */

/* ---------------- wake-up ----------------
   The RTC alarm (EXTI line 17) and PA10 (EXTI line 10) are used as EVENTS, not interrupts:
   WFE wakes the chip and no interrupt handler runs. So the wake-up does not depend on the
   handler names in the startup file (CubeIDE's names differ between versions), and nothing
   can get stuck in an unhandled interrupt. The flags are cleared here after waking. */

/* The first byte after a key wake-up is usually broken: its start bit came while the
   UART had no clock. The UART interrupt asks here and drops it. */
int power_uart_byte_ok(void)
{
    if (key_wake_recent) {
        key_wake_recent = 0;
        if ((xTaskGetTickCountFromISR() - key_wake_tick) <= pdMS_TO_TICKS(WAKE_GARBAGE_MS)) {
            return 0;
        }
    }
    return 1;
}

void power_console_activity(void)
{
    console_until = xTaskGetTickCountFromISR() + pdMS_TO_TICKS(CONSOLE_AWAKE_MS);
}

/* ---------------- set-up and mode ---------------- */

/* about 26 s: 4096 counts of 40 kHz / 256 (the LSI is 30..60 kHz, so 17..35 s) */
static void watchdog_start(void)
{
    uint32_t n = 0;
    DBGMCU_CR |= DBGMCU_IWDG_STOP;
    IWDG_KR  = IWDG_KEY_START;
    IWDG_KR  = IWDG_KEY_UNLOCK;
    IWDG_PR  = 6U;                                    /* divide by 256 */
    IWDG_RLR = 0xFFFU;
    while (IWDG_SR != 0U && ++n < 100000U) { }        /* the new values are taken over */
    IWDG_KR  = IWDG_KEY_FEED;
}

void power_init(void)
{
    watchdog_start();
    RCC_APB2ENR |= RCC_APB2ENR_AFIOEN;
    RCC_APB1ENR |= RCC_APB1ENR_PWREN;
    AFIO_EXTICR3 &= ~(0xFU << 8);                     /* EXTI line 10 = port A (PA10) */
    EXTI_FTSR |= EXTI_LINE10;                         /* falling edge: the start bit of a byte */
    EXTI_RTSR |= EXTI_LINE17;                         /* RTC alarm: rising edge */
    EXTI_IMR  &= ~(EXTI_LINE10 | EXTI_LINE17);        /* no interrupts from these lines */
    EXTI_EMR  &= ~EXTI_LINE10;                        /* the key event is armed only in STOP */
    EXTI_EMR  |= EXTI_LINE17;                         /* the alarm event wakes WFE */
    EXTI_PR    = EXTI_LINE10 | EXTI_LINE17;
}

void power_set_mode(power_mode_t m)
{
    mode = m;
}

power_mode_t power_mode(void)
{
    return mode;
}

const char *power_mode_name(power_mode_t m)
{
    return m == POWER_RUN ? "run" : m == POWER_SLEEP ? "sleep" : "stop";
}

int power_parse_mode(const char *s, power_mode_t *m)
{
    if (str_equal(s, "run"))   { *m = POWER_RUN;   return 1; }
    if (str_equal(s, "sleep")) { *m = POWER_SLEEP; return 1; }
    if (str_equal(s, "stop"))  { *m = POWER_STOP;  return 1; }
    return 0;
}

uint32_t power_stop_count(void) { return stop_count; }
uint32_t power_stop_ms(void)    { return (uint32_t)(((uint64_t)stop_rtc_total * 1000U) / RTC_HZ); }
uint32_t power_key_wakes(void)  { return key_wakes; }
uint32_t power_stats_ms(void)   { return (uint32_t)(xTaskGetTickCount() - stats_since); }

void power_stats_reset(void)
{
    taskENTER_CRITICAL();
    stop_count = 0;
    stop_rtc_total = 0;
    key_wakes = 0;
    stats_since = xTaskGetTickCount();
    taskEXIT_CRITICAL();
}

/* ---------------- the idle task ---------------- */

static void wait_for_interrupt(void)
{
    __asm volatile ("dsb\n wfi\n isb" ::: "memory");
}

/* SEV + WFE clears the event latch, the second WFE then really waits for the next event */
static void wait_for_event(void)
{
    __asm volatile ("dsb\n sev\n wfe\n wfe\n isb" ::: "memory");
}

static int stop_allowed(uint32_t ms)
{
    TickType_t now = xTaskGetTickCount();
    return ms >= STOP_MIN_MS
        && rtc_running()
        && !(DHCSR & DHCSR_C_DEBUGEN)
        && now >= pdMS_TO_TICKS(STOP_HOLDOFF_MS)
        && (int32_t)(console_until - now) <= 0;
}

static void tick_restart(void)
{
    SYST_RVR = TICK_RELOAD;
    SYST_CVR = 0;
    SYST_CSR = SYST_CSR_RUN;
}

/* 'power test': one STOP of exactly 1 s from the command line, to check that the RTC alarm
   wakes the chip. The FreeRTOS tick loses that second (all tasks just run 1 s later).
   Returns the RTC counts slept (1024 = 1 s), or -1 if the alarm could not be set. */
int32_t power_stop_test(void)
{
    uint32_t start, alarm, n = 0;
    int32_t slept;

    while (!uart_tx_idle() && ++n < 2000000U) { }    /* let the text leave first */
    vTaskSuspendAll();
    __asm volatile ("cpsid i" ::: "memory");
    SYST_CSR = 0;
    SCB_ICSR = SCB_ICSR_PENDSTCLR;
    start = rtc_counter();
    alarm = start + RTC_HZ;
    if (!rtc_set_alarm(alarm)) {
        tick_restart();
        __asm volatile ("cpsie i" ::: "memory");
        (void)xTaskResumeAll();
        return -1;
    }
    EXTI_PR = EXTI_LINE10 | EXTI_LINE17;
    PWR_CR = (PWR_CR & ~PWR_CR_PDDS) | PWR_CR_LPDS | PWR_CR_CWUF;
    SCB_SCR |= SCB_SCR_SLEEPDEEP;
    wait_for_event();                                 /* ---- STOP for 1 s ---- */
    SCB_SCR &= ~SCB_SCR_SLEEPDEEP;
    EXTI_PR = EXTI_LINE10 | EXTI_LINE17;
    rtc_clear_alarm();
    rtc_resync();
    slept = (int32_t)(rtc_counter() - start);
    IWDG_KR = IWDG_KEY_FEED;
    tick_restart();
    __asm volatile ("cpsie i" ::: "memory");
    (void)xTaskResumeAll();
    return slept;
}

/* Called by the idle task with the scheduler paused. expected_ms = time until a task is due. */
void power_idle(uint32_t expected_ms)
{
    uint32_t start, alarm, now, counts, slept_ms;
    int woke_by_key;

    IWDG_KR = IWDG_KEY_FEED;                          /* the idle task runs: nothing is stuck */
    if (mode == POWER_RUN) {
        return;                                       /* the idle task keeps spinning */
    }
    if (mode == POWER_SLEEP || !stop_allowed(expected_ms)) {
        wait_for_interrupt();                         /* SLEEP: the 1 ms tick wakes us */
        return;
    }
    if (expected_ms > STOP_MAX_MS) {
        expected_ms = STOP_MAX_MS;
    }

    __asm volatile ("cpsid i" ::: "memory");          /* no interrupt may run until we decide */
    if (eTaskConfirmSleepModeStatus() == eAbortSleep || !uart_tx_idle()) {
        __asm volatile ("cpsie i" ::: "memory");      /* a task got ready, or text is still going out */
        return;
    }

    SYST_CSR = 0;                                     /* stop the 1 ms tick */
    SCB_ICSR = SCB_ICSR_PENDSTCLR;

    /* alarm 1 ms early: after waking, SLEEP covers the rest exactly */
    counts = ((expected_ms - 1U) * RTC_HZ) / 1000U;
    start = rtc_counter();
    alarm = start + counts;
    if (counts < 3U || !rtc_set_alarm(alarm) || (int32_t)(alarm - rtc_counter()) < 2) {
        tick_restart();                               /* too close: the alarm could be missed */
        __asm volatile ("cpsie i" ::: "memory");
        return;
    }

    EXTI_PR = EXTI_LINE10 | EXTI_LINE17;
    EXTI_EMR |= EXTI_LINE10;                          /* a key may wake us too */
    PWR_CR = (PWR_CR & ~PWR_CR_PDDS) | PWR_CR_LPDS | PWR_CR_CWUF;
    SCB_SCR |= SCB_SCR_SLEEPDEEP;
    wait_for_event();                                 /* ---- STOP: wakes on EXTI 17 or 10 ---- */
    SCB_SCR &= ~SCB_SCR_SLEEPDEEP;
    EXTI_EMR &= ~EXTI_LINE10;
    EXTI_PR = EXTI_LINE10 | EXTI_LINE17;
    rtc_clear_alarm();
    /* the clock is the 8 MHz HSI again after STOP: that is what we use anyway */

    rtc_resync();                                     /* RTC registers are stale after STOP */
    now = rtc_counter();
    counts = now - start;
    woke_by_key = (int32_t)(alarm - now) > 1;         /* woke before the alarm: only a key can do that */
    carry += counts * 1000U;                          /* in 1/1024 ms: keeps the parts of a ms */
    slept_ms = carry / RTC_HZ;
    carry -= slept_ms * RTC_HZ;
    if (slept_ms >= expected_ms) {
        slept_ms = expected_ms - 1U;                  /* never step past the task that is due */
        carry = 0;
    }
    stop_count++;
    stop_rtc_total += counts;

    IWDG_KR = IWDG_KEY_FEED;
    tick_restart();
    vTaskStepTick(slept_ms);                          /* FreeRTOS time catches up */
    if (woke_by_key) {
        key_wakes++;
        key_wake_tick = xTaskGetTickCount();
        key_wake_recent = 1;
        console_until = key_wake_tick + pdMS_TO_TICKS(CONSOLE_AWAKE_MS);
    }
    __asm volatile ("cpsie i" ::: "memory");
}
