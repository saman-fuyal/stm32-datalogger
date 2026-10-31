/* Real time clock on the 32.768 kHz crystal (LSE).
 *
 * The counter runs at 1024 Hz (not 1 Hz) so that the STOP-mode alarm can wake the chip to
 * the millisecond and the time asleep can be measured to the millisecond.
 * Seconds since 1970 = base + counter / 1024. The base is kept in two backup registers
 * (BKP_DR2, DR3), so with a battery on VBAT both survive a power cut.
 * The 32-bit counter would overflow after 48 days at 1024 Hz, so every 24 days the whole
 * seconds are moved from the counter into the base.
 */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
#include "rtc.h"

#define RTC_MAGIC     0xA5A6U       /* BKP_DR1: set up with the 1024 Hz counter (0xA5A5 = old 1 Hz) */
#define RTC_PRESCALER 31U           /* 32768 / (31 + 1) = 1024 counts per second */
#define REBASE_AT     0x80000000UL  /* about 24 days of counting */

static uint8_t rtc_ok = 0;
static uint32_t lse_start_ms = 0;          /* how long the crystal took to start (0 = was running) */

static void rtc_wait_write(void)
{
    wait_bits(&RTC_CRL, RTC_CRL_RTOFF, 1, 100U);     /* the last write has reached the RTC */
}

static void backup_access_on(void)
{
    RCC_APB1ENR |= RCC_APB1ENR_PWREN | RCC_APB1ENR_BKPEN;
    PWR_CR |= PWR_CR_DBP;                              /* allow writing the backup area */
}

static uint32_t base_get(void)
{
    return ((BKP_DR2 & 0xFFFFU) << 16) | (BKP_DR3 & 0xFFFFU);
}

static void base_set(uint32_t secs)
{
    BKP_DR2 = secs >> 16;
    BKP_DR3 = secs & 0xFFFFU;
}

static void counter_write(uint32_t v)                  /* config mode, then wait until done */
{
    rtc_wait_write();
    RTC_CRL |= RTC_CRL_CNF;
    RTC_CNTH = v >> 16;
    RTC_CNTL = v & 0xFFFFU;
    RTC_CRL &= ~RTC_CRL_CNF;
    rtc_wait_write();
}

int rtc_needs_setup(void)
{
    backup_access_on();
    return BKP_DR1 != RTC_MAGIC;
}

void rtc_resync(void)
{
    RTC_CRL &= ~RTC_CRL_RSF;
    wait_bits(&RTC_CRL, RTC_CRL_RSF, 1, 10U);          /* registers valid again (about 30 us) */
}

int rtc_init(void)
{
    backup_access_on();

    if (BKP_DR1 == RTC_MAGIC) {                        /* already set up: it kept running */
        rtc_resync();
        rtc_ok = 1;
        return 1;
    }

    RCC_BDCR |= RCC_BDCR_BDRST;                        /* start from a clean backup area */
    RCC_BDCR &= ~RCC_BDCR_BDRST;
    RCC_BDCR |= RCC_BDCR_LSEON;
    lse_start_ms = tick_ms();
    if (!wait_bits(&RCC_BDCR, RCC_BDCR_LSERDY, 1, 10000U)) {  /* cheap crystals can start slowly */
        lse_start_ms = tick_ms() - lse_start_ms;
        RCC_BDCR &= ~RCC_BDCR_LSEON;
        rtc_ok = 0;
        return 0;
    }
    lse_start_ms = tick_ms() - lse_start_ms;
    RCC_BDCR |= RCC_BDCR_RTCSEL_LSE;                   /* the RTC counts the 32.768 kHz crystal */
    RCC_BDCR |= RCC_BDCR_RTCEN;
    rtc_resync();

    rtc_wait_write();
    RTC_CRL |= RTC_CRL_CNF;                            /* configuration mode */
    RTC_PRLH = 0;
    RTC_PRLL = RTC_PRESCALER;
    RTC_CNTH = 0;
    RTC_CNTL = 0;
    RTC_CRL &= ~RTC_CRL_CNF;
    rtc_wait_write();

    base_set(0);
    BKP_DR1 = RTC_MAGIC;                               /* remember that it is set up */
    rtc_ok = 1;
    return 1;
}

uint32_t rtc_start_ms(void)
{
    return lse_start_ms;
}

int rtc_running(void)
{
    return rtc_ok;
}

uint32_t rtc_counter(void)
{
    uint32_t h1, l, h2;
    do {                                               /* read again if the high half changed */
        h1 = RTC_CNTH;
        l  = RTC_CNTL;
        h2 = RTC_CNTH;
    } while (h1 != h2);
    return (h1 << 16) | l;
}

/* The scheduler is paused while the counter and the base change, so no other task (and not
   the STOP code in the idle task) sees one changed without the other. */
static int pause_tasks(void)                           /* 1 = we paused it */
{
    if (rtos_running()) {
        vTaskSuspendAll();
        return 1;
    }
    return 0;
}

static void resume_tasks(int paused)
{
    if (paused) {
        (void)xTaskResumeAll();
    }
}

/* Without the crystal there is no RTC. Then a software clock is used: seconds = base + tick.
   It works for time stamps and 'set time', but is only as exact as the 8 MHz HSI (about 1 %),
   stops at a reset, and STOP mode is not possible (power.c uses SLEEP). */
static uint32_t soft_base = 0;

static uint32_t soft_seconds(void)
{
    return soft_base + tick_ms() / 1000U;
}

uint32_t rtc_get(void)
{
    uint32_t c;
    if (!rtc_ok) {
        return soft_seconds();
    }
    c = rtc_counter();
    if (c >= REBASE_AT) {                              /* move whole seconds into the base */
        uint32_t secs;
        int paused = pause_tasks();
        c = rtc_counter();
        secs = c / RTC_HZ;
        counter_write(c - secs * RTC_HZ);
        base_set(base_get() + secs);
        resume_tasks(paused);
        c = rtc_counter();
    }
    return base_get() + c / RTC_HZ;
}

void rtc_set(uint32_t secs)
{
    int paused;
    if (!rtc_ok) {
        soft_base = secs - tick_ms() / 1000U;
        return;
    }
    paused = pause_tasks();
    counter_write(0);
    base_set(secs);
    resume_tasks(paused);
}

int rtc_clock_is_set(void)
{
    return rtc_get() >= RTC_MIN_VALID;
}

int rtc_set_alarm(uint32_t count)
{
    /* ALRIE lets the alarm reach EXTI line 17. RTC_CRH is cleared by every reset (unlike the
       counter and the alarm value), so it is set here each time, not only at the first set-up. */
    RTC_CRH |= RTC_CRH_ALRIE;
    rtc_wait_write();
    RTC_CRL |= RTC_CRL_CNF;
    RTC_ALRH = count >> 16;
    RTC_ALRL = count & 0xFFFFU;
    RTC_CRL &= ~(RTC_CRL_CNF | RTC_CRL_ALRF);
    if (!wait_bits(&RTC_CRL, RTC_CRL_RTOFF, 1, 5U)) {
        return 0;
    }
    return 1;
}

void rtc_clear_alarm(void)
{
    RTC_CRL &= ~RTC_CRL_ALRF;
}

/* ---------------- calendar maths ---------------- */

uint32_t days_from_date(int32_t y, int32_t m, int32_t d)
{
    int32_t era, yoe, doy, doe;
    if (m <= 2) {
        y--;
    }
    era = y / 400;
    yoe = y - era * 400;
    doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (uint32_t)(era * 146097 + doe - 719468);
}

void date_from_days(uint32_t z, int32_t *y, int32_t *m, int32_t *d)
{
    int32_t zz  = (int32_t)z + 719468;
    int32_t era = zz / 146097;
    int32_t doe = zz - era * 146097;
    int32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int32_t mp  = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = (mp < 10) ? mp + 3 : mp - 9;
    *y = yoe + era * 400 + ((*m <= 2) ? 1 : 0);
}

static uint32_t days_in_month(uint32_t y, uint32_t m)
{
    static const uint8_t days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m == 2U && ((y % 4U == 0U && y % 100U != 0U) || y % 400U == 0U)) {
        return 29U;                                    /* leap year */
    }
    return days[m - 1U];
}

static int parse_digits(const char *s, uint8_t n, uint32_t *out)
{
    uint32_t v = 0;
    while (n--) {
        if (*s < '0' || *s > '9') {
            return 0;
        }
        v = v * 10U + (uint32_t)(*s - '0');
        s++;
    }
    *out = v;
    return 1;
}

int parse_datetime(const char *s, uint32_t *secs)
{
    uint32_t y, mo, d, h, mi, se;
    if (!parse_digits(s, 4, &y)       || s[4]  != '-' ||
        !parse_digits(s + 5, 2, &mo)  || s[7]  != '-' ||
        !parse_digits(s + 8, 2, &d)   || s[10] != ' ' ||
        !parse_digits(s + 11, 2, &h)  || s[13] != ':' ||
        !parse_digits(s + 14, 2, &mi) || s[16] != ':' ||
        !parse_digits(s + 17, 2, &se) || s[19] != '\0') {
        return 0;
    }
    if (y < 2024U || y > 2099U || mo < 1U || mo > 12U || d < 1U ||
        d > days_in_month(y, mo) || h > 23U || mi > 59U || se > 59U) {
        return 0;
    }
    *secs = days_from_date((int32_t)y, (int32_t)mo, (int32_t)d) * 86400U
            + h * 3600U + mi * 60U + se;
    return 1;
}
