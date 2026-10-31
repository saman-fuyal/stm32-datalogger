/* Samples, the readable 'read' line, the CSV log line, start / stop. */
#include "adc.h"
#include "app.h"
#include "board.h"
#include "bmp280.h"
#include "config.h"
#include "fmt.h"
#include "locks.h"
#include "logger.h"
#include "mpu6050.h"
#include "power.h"
#include "rtc.h"
#include "sdlog.h"
#include "timing.h"
#include "uart.h"

/* SensorTask and LoggerTask take the UART only with these timeouts (see locks.h).
   If the CLI is busy printing (for example 'cat'), the message is skipped, not delayed. */
#define MSG_WAIT_MS   50U
#define LINE_WAIT_MS  100U

static volatile uint8_t  running = 0;
static volatile uint32_t rate_ms = RATE_MS_DEFAULT;
static uint32_t last_sd_try = 0;
static uint32_t last_blink = 0;
static uint32_t uart_skipped = 0;

/* ---------------- sensors (the caller holds LOCK_I2C) ---------------- */

int sensors_mpu_init(void)
{
    uint8_t who;
    int ok = mpu_init(&who);
    if (who != 0U && uart_lock(MSG_WAIT_MS)) {
        fmt_str(CON, "MPU WHO_AM_I = 0x");
        fmt_hex8(CON, who);
        fmt_str(CON, who == 0x68U ? " (MPU6050)\r\n" : " (MPU6050-compatible)\r\n");
        uart_unlock();
    }
    return ok;
}

int sensors_ensure(void)
{
    if (!bmp_ready()) {
        bmp_init();
    }
    if (!mpu_ready()) {
        sensors_mpu_init();
    }
    return bmp_ready() && mpu_ready();
}

void report_mpu_restart(void)
{
    if (mpu_take_restart_event() && uart_lock(MSG_WAIT_MS)) {
        fmt_str(CON, "[MPU lost its settings, restarted]\r\n");
        uart_unlock();
    }
}

int take_sample(sample_t *s)
{
    int16_t v[7];
    int r;

    r = bmp_read(&s->temp, &s->press);
    if (r != BMP_OK) {
        if (uart_lock(MSG_WAIT_MS)) {
            fmt_str(CON, "BMP280 read error, code ");
            fmt_u32(CON, (uint32_t)r);
            fmt_str(CON, " (");
            fmt_str(CON, bmp_err_text(r));
            fmt_str(CON, ")\r\n");
            uart_unlock();
        }
        return 0;
    }
    r = mpu_read_raw(v);
    report_mpu_restart();
    if (r != MPU_OK) {
        if (uart_lock(MSG_WAIT_MS)) {
            fmt_str(CON, "MPU6050 read error\r\n");
            uart_unlock();
        }
        return 0;
    }
    mpu_scale(v, s->acc, s->gyro);
    s->adc_valid = (uint8_t)adc_read_chip(&s->vdd_mv, &s->chip_c100);
    if (s->adc_valid) {
        s->chip_c100 += adc_ts_offset();
    }
    return 1;
}

/* the 'read' command: the caller holds LOCK_UART and LOCK_I2C */
void print_sample_text(void)
{
    sample_t s;
    uint32_t m2;

    if (!take_sample(&s)) {
        return;
    }
    m2 = (uint32_t)(s.acc[0] * s.acc[0] + s.acc[1] * s.acc[1] + s.acc[2] * s.acc[2]);

    fmt_char(CON, '[');
    fmt_datetime(CON, rtc_get());
    fmt_str(CON, "] Temp ");
    fmt_fixed(CON, s.temp, 2);
    fmt_str(CON, " C | Press ");
    fmt_fixed(CON, (int32_t)s.press, 2);
    fmt_str(CON, " hPa | Acc ");
    fmt_vec3(CON, s.acc, 3);
    fmt_str(CON, " g (|a| ");
    fmt_fixed(CON, (int32_t)isqrt32(m2), 3);
    fmt_str(CON, ") | Gyro ");
    fmt_vec3(CON, s.gyro, 2);
    fmt_str(CON, " dps | VDD ");
    if (s.adc_valid) {
        fmt_fixed(CON, (s.vdd_mv + 5) / 10, 2);        /* rounded, not cut off */
        fmt_str(CON, " V | Chip ");
        fmt_fixed(CON, s.chip_c100, 2);
        fmt_str(CON, " C\r\n");
    } else {
        fmt_str(CON, "n/a\r\n");
    }
}

/* ---------------- SensorTask ---------------- */

int logger_take_record(record_t *r)
{
    int ok;
    r->t_start = timing_now_us();
    r->stamp = rtc_get();                         /* RTC, or the software clock without a crystal */
    lock_take(LOCK_I2C, LOCK_FOREVER);
    ok = take_sample(&r->s);
    lock_give(LOCK_I2C);
    return ok;
}

/* ---------------- LoggerTask ---------------- */

/* builds  <values>*CRC32\r\n  in text */
static void build_line(const record_t *r, char *text, uint16_t size)
{
    outbuf_t line;
    const sample_t *s = &r->s;
    uint32_t crc;
    uint8_t i;

    ob_init(&line, text, size);
    fmt_datetime(&line, r->stamp);
    fmt_char(&line, ',');
    fmt_fixed(&line, s->temp, 2);
    fmt_char(&line, ',');
    fmt_fixed(&line, (int32_t)s->press, 2);
    for (i = 0; i < 3; i++) {
        fmt_char(&line, ',');
        fmt_fixed(&line, s->acc[i], 3);
    }
    for (i = 0; i < 3; i++) {
        fmt_char(&line, ',');
        fmt_fixed(&line, s->gyro[i], 2);
    }
    fmt_char(&line, ',');
    if (s->adc_valid) {
        fmt_fixed(&line, (s->vdd_mv + 5) / 10, 2);    /* rounded, not cut off */
    } else {
        fmt_str(&line, "nan");
    }
    fmt_char(&line, ',');
    if (s->adc_valid) {
        fmt_fixed(&line, s->chip_c100, 2);
    } else {
        fmt_str(&line, "nan");
    }
    crc = crc32_str(text);                       /* checksum of the values only */
    fmt_char(&line, '*');
    fmt_hex32(&line, crc);
    fmt_str(&line, "\r\n");
}

static void warn(const char *a, const char *b, const char *c)   /* one message, UART with timeout */
{
    if (uart_lock(LINE_WAIT_MS)) {
        fmt_str(CON, a);
        fmt_str(CON, b);
        fmt_str(CON, c);
        uart_unlock();
    }
}

void logger_write_record(const record_t *r)
{
    char text[128];
    uint32_t t0;
    int ok = 1, wrote = 0;

    t0 = timing_now_us();
    build_line(r, text, sizeof(text));
    timing_record(TM_FORMAT, timing_now_us() - t0);

    t0 = timing_now_us();
    if (uart_lock(LINE_WAIT_MS)) {
        fmt_str(CON, text);
        uart_unlock();
    } else {
        uart_skipped++;
    }
    timing_record(TM_UART, timing_now_us() - t0);

    t0 = timing_now_us();
    lock_take(LOCK_SD, LOCK_FOREVER);
    if (running && sdlog_is_open()) {            /* 'stop' may have closed the file meanwhile */
        ok = sdlog_write(text);
        wrote = ok;
    }
    lock_give(LOCK_SD);
    if (wrote) {
        timing_record(TM_SD, timing_now_us() - t0);
        timing_record(TM_LATENCY, timing_now_us() - r->t_start);
    }
    if (!ok) {
        warn("SD write failed (", sdlog_result_text(sdlog_last_result()),
             "), logging to the UART only. Retrying every 5 s.\r\n");
        last_sd_try = tick_ms();
    }
}

/* tries to open a new log file and prints what happened. The caller holds LOCK_SD. */
static int open_log_file(void)
{
    if (sdlog_open_new(LOG_HEADER)) {
        warn("Logging to ", sdlog_filename(), "\r\n");
        return 1;
    }
    return 0;
}

void logger_housekeeping(void)
{
    uint32_t now = tick_ms();
    int open;

    if (!running) {
        return;
    }
    lock_take(LOCK_SD, LOCK_FOREVER);
    if (!running) {                               /* stopped while we waited for the lock */
        lock_give(LOCK_SD);
        return;
    }
    sdlog_sync_if_due();
    open = sdlog_is_open();
    if (!open && (now - last_sd_try) >= SD_RETRY_MS) {
        last_sd_try = now;
        if (sdlog_mount() && open_log_file()) {
            open = 1;
        }
    }
    lock_give(LOCK_SD);

    if (!open && (now - last_blink) >= 250U) {    /* no card: LED blinks twice a second */
        last_blink = now;
        led_toggle();
    } else if (open) {
        logger_led_update();
    }
}

/* LED: off when stopped; fast blink without a card (logger_housekeeping); with a card it is
   on all the time, except in STOP mode, where it only flashes during each sample (saves mA). */
void logger_led_update(void)
{
    if (!running) {
        led_off();
    } else if (sdlog_is_open()) {
        if (power_mode() == POWER_STOP) {
            led_off();
        } else {
            led_on();
        }
    }
}

void logger_led_pulse(int on)                     /* SensorTask, around each sample */
{
    if (running && power_mode() == POWER_STOP && sdlog_is_open()) {
        if (on) {
            led_on();
        } else {
            led_off();
        }
    }
}

uint32_t logger_wait_ms(void)
{
    if (!running) {
        return LOGGER_WAIT_FOREVER;
    }
    if (!sdlog_is_open()) {
        return 250U;                              /* blink and retry the card */
    }
    return sdlog_ms_until_sync();                 /* SDLOG_NO_SYNC_DUE = LOGGER_WAIT_FOREVER */
}

/* ---------------- start / stop (CliTask, or the start-up task) ---------------- */

void logger_start(void)
{
    int ok;

    uart_lock(UART_WAIT_FOREVER);
    if (running) {
        fmt_str(CON, "ERR already logging (type stop first)\r\n");
        uart_unlock();
        return;
    }
    lock_take(LOCK_I2C, LOCK_FOREVER);
    ok = sensors_ensure();
    lock_give(LOCK_I2C);
    if (!ok) {
        fmt_str(CON, "ERR sensors not ready, check wiring (try scan)\r\n");
        uart_unlock();
        return;
    }
    if (!rtc_clock_is_set()) {
        fmt_str(CON, "WARNING: clock not set, the dates will start at 1970 (use set time)\r\n");
    }
    lock_take(LOCK_SD, LOCK_FOREVER);
    ok = open_log_file();
    lock_give(LOCK_SD);
    if (!ok) {
        fmt_str(CON, "WARNING: no SD card (");
        fmt_str(CON, sdlog_result_text(sdlog_last_result()));
        fmt_str(CON, "), logging to the UART only. Retrying every 5 s.\r\n");
    }
    app_flush_records();                          /* nothing old from the last run */
    last_sd_try = tick_ms();
    fmt_str(CON, "OK started\r\n");
    fmt_str(CON, LOG_HEADER "\r\n");
    uart_unlock();
    /* only now: printing can make this task wait, and SensorTask must not start early
       (it would then also get the wake-up below and take two samples in one second) */
    running = 1;
    logger_led_update();
    app_wake_sensor();                            /* the first line comes at once */
}

void logger_stop(void)
{
    uart_lock(UART_WAIT_FOREVER);
    if (!running) {
        fmt_str(CON, "OK stopped (was not logging)\r\n");
        uart_unlock();
        return;
    }
    running = 0;
    app_wake_sensor();                            /* so SensorTask goes back to waiting */
    led_off();
    lock_take(LOCK_SD, LOCK_FOREVER);
    if (sdlog_is_open()) {
        fmt_str(CON, "OK stopped, ");
        fmt_str(CON, sdlog_filename());
        fmt_str(CON, " closed (");
        fmt_u32(CON, sdlog_lines());
        fmt_str(CON, " lines)\r\n");
        sdlog_close();
    } else {
        fmt_str(CON, "OK stopped\r\n");
    }
    lock_give(LOCK_SD);
    uart_unlock();
}

int logger_running(void)
{
    return running;
}

uint32_t logger_rate(void)
{
    return rate_ms;
}

void logger_set_rate(uint32_t ms)
{
    rate_ms = ms;
    app_wake_sensor();                            /* start the new rate now */
}

uint32_t logger_uart_skipped(void)
{
    return uart_skipped;
}
