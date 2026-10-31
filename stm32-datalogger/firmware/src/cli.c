/*
 * Command line. Every command is one row in the table at the bottom: its name,
 * the function that runs it, and its help text. 'help' prints the table, so the
 * help can never get out of date.
 * A name ending in a space ("set rate ") takes the rest of the line as its argument.
 *
 * Runs in CliTask. While a command runs, CliTask holds the UART lock (so its output is never
 * mixed with log lines) and the locks named in the table (I2C for the sensors, SD for the
 * card). Log lines that come meanwhile still go to the SD card; only their UART copy is
 * skipped ('tasks' counts them).
 */
#include "FreeRTOS.h"
#include "task.h"
#include "adc.h"
#include "app.h"
#include "board.h"
#include "bmp280.h"
#include "cli.h"
#include "config.h"
#include "ff.h"
#include "fmt.h"
#include "i2c.h"
#include "locks.h"
#include "logger.h"
#include "mpu6050.h"
#include "power.h"
#include "rtc.h"
#include "sd.h"
#include "sdlog.h"
#include "timing.h"
#include "uart.h"

/* ---------------- normal commands ---------------- */

static void cmd_start(const char *arg) { (void)arg; logger_start(); }
static void cmd_stop(const char *arg)  { (void)arg; logger_stop(); }

static void cmd_set_rate(const char *arg)
{
    uint32_t n;
    if (parse_u32(arg, &n, RATE_MS_MAX) && n >= RATE_MS_MIN) {
        logger_set_rate(n);
        fmt_str(CON, "OK rate = ");
        fmt_u32(CON, n);
        fmt_str(CON, " ms\r\n");
    } else {
        fmt_str(CON, "ERR rate must be a number from 250 to 60000\r\n");
    }
}

static void cmd_set_time(const char *arg)
{
    uint32_t secs;
    if (parse_datetime(arg, &secs)) {
        rtc_set(secs);
        fmt_str(CON, "OK time set to ");
        fmt_datetime(CON, rtc_get());
        fmt_str(CON, rtc_running() ? "\r\n"
                                   : " (software clock: the 32 kHz crystal is not running)\r\n");
    } else {
        fmt_str(CON, "ERR use: set time YYYY-MM-DD HH:MM:SS\r\n");
    }
}

static void cmd_time(const char *arg)
{
    (void)arg;
    fmt_datetime(CON, rtc_get());
    fmt_str(CON, rtc_running() ? "\r\n" : " (software clock)\r\n");
}

static void cmd_read(const char *arg)
{
    (void)arg;
    if (sensors_ensure()) {
        print_sample_text();
    } else {
        fmt_str(CON, "ERR sensors not ready, check wiring (try scan)\r\n");
    }
}

static void cmd_zero(const char *arg)
{
    (void)arg;
    if (!mpu_ready() && !sensors_mpu_init()) {
        fmt_str(CON, "ERR MPU6050 not ready, check wiring\r\n");
        return;
    }
    fmt_str(CON, "Keep the sensor still for 1 second...\r\n");
    fmt_str(CON, gyro_zero() ? "OK gyro zeroed\r\n" : "ERR too many read errors\r\n");
    report_mpu_restart();
}

static void cmd_status(const char *arg)
{
    (void)arg;
    fmt_str(CON, logger_running() ? "running" : "stopped");
    fmt_str(CON, ", rate = ");
    fmt_u32(CON, logger_rate());
    fmt_str(CON, " ms, BMP280 ");
    fmt_str(CON, bmp_ready() ? "ok" : "not ready");
    fmt_str(CON, ", MPU6050 ");
    fmt_str(CON, mpu_ready() ? "ok" : "not ready");
    fmt_str(CON, ", MPU restarts = ");
    fmt_u32(CON, mpu_restart_count());
    fmt_str(CON, ", BMP retries = ");
    fmt_u32(CON, bmp_retry_count());
    fmt_str(CON, ", ADC ");
    fmt_str(CON, adc_running() ? "ok" : "not running");
    fmt_str(CON, ", clock ");
    fmt_str(CON, rtc_running() ? "ok" : "NOT running");
    fmt_str(CON, ", I2C bus recoveries = ");
    fmt_u32(CON, i2c_recovery_count());
    fmt_str(CON, "\r\nSD: ");
    if (sdlog_is_open()) {
        fmt_str(CON, sdlog_filename());
        fmt_str(CON, ", ");
        fmt_u32(CON, sdlog_lines());
        fmt_str(CON, " lines");
    } else {
        fmt_str(CON, sdlog_mounted() ? "mounted, no file open" : "not mounted");
    }
    fmt_str(CON, ", errors = ");
    fmt_u32(CON, sdlog_errors());
    fmt_str(CON, "\r\n");
}

static void cmd_chip(const char *arg)
{
    int32_t vdd, raw, bt;
    uint32_t bp;
    (void)arg;

    if (!adc_read_chip(&vdd, &raw)) {
        fmt_str(CON, "ERR ADC is not running\r\n");
        return;
    }
    fmt_str(CON, "Supply = ");
    fmt_fixed(CON, (vdd + 5) / 10, 2);              /* rounded, not cut off */
    fmt_str(CON, " V, chip temperature: raw ");
    fmt_fixed(CON, raw, 2);
    fmt_str(CON, " C, corrected ");
    fmt_fixed(CON, raw + adc_ts_offset(), 2);
    fmt_str(CON, " C");
    if (bmp_ready() && bmp_read_once(&bt, &bp) == BMP_OK) {
        fmt_str(CON, ", BMP280 air ");
        fmt_fixed(CON, bt, 2);
        fmt_str(CON, " C");
    }
    fmt_str(CON, "\r\n");
}

static void cmd_sd_init(const char *arg)
{
    uint8_t t;
    int r;
    (void)arg;

    if (sdlog_is_open()) {
        fmt_str(CON, "ERR the log file is open, type stop first\r\n");
        return;
    }
    r = sd_init();
    if (r != SD_OK) {
        fmt_str(CON, "ERR ");
        fmt_u32(CON, (uint32_t)r);
        fmt_str(CON, ": ");
        fmt_str(CON, sd_error_text(r));
        fmt_str(CON, "\r\n");
        return;
    }
    t = sd_card_type();
    fmt_str(CON, "Card started: ");
    fmt_str(CON, t == SD_V2_BLOCK ? "SDHC/SDXC (block addressing)" :
                 t == SD_V2       ? "SD version 2 (byte addressing)" :
                                    "SD version 1 (byte addressing)");
    fmt_str(CON, "\r\nSize: ");
    fmt_u32(CON, sd_sector_count() / 2048U);
    fmt_str(CON, " MB\r\n");
}

static void print_fr_error(int fr)
{
    fmt_str(CON, "ERR ");
    fmt_str(CON, sdlog_result_text(fr));
    fmt_str(CON, "\r\n");
}

static void cmd_sd_mount(const char *arg)
{
    (void)arg;
    if (sdlog_is_open()) {
        fmt_str(CON, "OK already mounted, logging to ");
        fmt_str(CON, sdlog_filename());
        fmt_str(CON, "\r\n");
    } else if (sdlog_mount()) {
        fmt_str(CON, "OK card mounted\r\n");
    } else {
        print_fr_error(sdlog_last_result());
    }
}

static int need_card(void)                     /* mounts the card if needed. 1 = ok */
{
    if (sdlog_mounted() || sdlog_mount()) {
        return 1;
    }
    print_fr_error(sdlog_last_result());
    return 0;
}

static void cmd_ls(const char *arg)
{
    DIR dir;
    FILINFO info;
    FRESULT fr;
    uint32_t count = 0;
    (void)arg;

    if (!need_card()) {
        return;
    }
    sdlog_sync();                               /* so the open file shows its real size */
    fr = f_opendir(&dir, "/");
    if (fr != FR_OK) {
        print_fr_error((int)fr);
        return;
    }
    while (f_readdir(&dir, &info) == FR_OK && info.fname[0] != '\0') {
        fmt_str(CON, info.fname);
        fmt_str(CON, (info.fattrib & AM_DIR) ? "  <dir>" : "  ");
        if (!(info.fattrib & AM_DIR)) {
            fmt_u32(CON, (uint32_t)info.fsize);
            fmt_str(CON, " bytes");
        }
        fmt_str(CON, "\r\n");
        count++;
    }
    (void)f_closedir(&dir);
    fmt_u32(CON, count);
    fmt_str(CON, " entries\r\n");
}

/* prints a file; any key stops it (a long log takes a while at 9600 baud).
   It takes the SD lock for one 64-byte piece at a time, so logging to the card goes on. */
static void cmd_cat(const char *arg)
{
    FIL f;
    FRESULT fr = FR_OK;
    char buf[64];
    UINT n = 0, i;
    uint8_t key;
    char prev = 0;
    int ok;

    lock_take(LOCK_SD, LOCK_FOREVER);
    ok = need_card();
    if (ok) {
        sdlog_sync();
        fr = f_open(&f, arg, FA_READ);
    }
    lock_give(LOCK_SD);
    if (!ok) {
        return;
    }
    if (fr != FR_OK) {
        print_fr_error((int)fr);
        return;
    }
    uart_rx_flush();
    do {
        lock_take(LOCK_SD, LOCK_FOREVER);
        fr = f_read(&f, buf, sizeof(buf), &n);
        lock_give(LOCK_SD);
        for (i = 0; i < n; i++) {
            if (buf[i] == '\n' && prev != '\r') {      /* the file may use \n or \r\n */
                fmt_char(CON, '\r');
            }
            fmt_char(CON, buf[i]);
            prev = buf[i];
        }
        if (uart_getc(&key)) {
            fmt_str(CON, "\r\n(stopped)\r\n");
            break;
        }
    } while (fr == FR_OK && n == sizeof(buf));
    lock_take(LOCK_SD, LOCK_FOREVER);
    (void)f_close(&f);
    lock_give(LOCK_SD);
}

/* ---------------- FreeRTOS commands ---------------- */

static void pad(uint8_t used, uint8_t width)        /* spaces up to the column */
{
    while (used++ < width) {
        fmt_char(CON, ' ');
    }
}

static uint8_t digits(uint32_t v)
{
    uint8_t n = 1;
    while (v >= 10U) {
        v /= 10U;
        n++;
    }
    return n;
}

static void fmt_u32_right(uint32_t v, uint8_t width)  /* number, right-aligned */
{
    pad(digits(v), width);
    fmt_u32(CON, v);
}

static const char *state_text(eTaskState st)
{
    switch (st) {
    case eRunning:   return "running";
    case eReady:     return "ready";
    case eBlocked:                          /* waiting with a timeout */
    case eSuspended: return "waiting";      /* waiting with no timeout (portMAX_DELAY) */
    default:         return "deleted";
    }
}

/* every task: priority, state, stack never used so far, share of the CPU time since power-on */
static void cmd_tasks(const char *arg)
{
    static TaskStatus_t st[8];
    configRUN_TIME_COUNTER_TYPE total;
    UBaseType_t n, i;
    (void)arg;

    n = uxTaskGetSystemState(st, 8, &total);
    fmt_str(CON, "Task      Prio  State     Stack free   CPU (of the time awake)\r\n");
    for (i = 0; i < n; i++) {
        fmt_str(CON, st[i].pcTaskName);
        pad(str_len(st[i].pcTaskName), 10);
        fmt_u32(CON, st[i].uxCurrentPriority);
        pad(1, 6);
        fmt_str(CON, state_text(st[i].eCurrentState));
        pad(str_len(state_text(st[i].eCurrentState)), 8);
        fmt_u32_right((uint32_t)st[i].usStackHighWaterMark * 4U, 7);
        fmt_str(CON, " B");
        if (total > 0U) {
            fmt_str(CON, "  ");
            fmt_u32_right((uint32_t)((st[i].ulRunTimeCounter * 1000U) / total) / 10U, 3);
            fmt_char(CON, '.');
            fmt_u32(CON, (uint32_t)((st[i].ulRunTimeCounter * 1000U) / total) % 10U);
            fmt_str(CON, " %");
        }
        fmt_str(CON, "\r\n");
    }
    fmt_str(CON, "Heap: ");
    fmt_u32(CON, (uint32_t)xPortGetFreeHeapSize());
    fmt_str(CON, " B free, lowest ever ");
    fmt_u32(CON, (uint32_t)xPortGetMinimumEverFreeHeapSize());
    fmt_str(CON, " B, of ");
    fmt_u32(CON, configTOTAL_HEAP_SIZE);
    fmt_str(CON, "\r\nRecords dropped (queue full): ");
    fmt_u32(CON, app_dropped_records());
    fmt_str(CON, ", log lines not printed (UART busy): ");
    fmt_u32(CON, logger_uart_skipped());
    fmt_str(CON, "\r\n");
    if (total == 0U) {
        fmt_str(CON, "(no CPU numbers: the DWT cycle counter is not running)\r\n");
    }
}

static void cmd_timing(const char *arg)
{
    tm_stat_t t;
    uint8_t i;
    (void)arg;

    fmt_str(CON, "microseconds        last       min       max    count\r\n");
    for (i = 0; i < TM_COUNT; i++) {
        timing_get((tm_id_t)i, &t);
        fmt_str(CON, timing_name((tm_id_t)i));
        pad(str_len(timing_name((tm_id_t)i)), 14);
        fmt_u32_right(t.last, 10);
        fmt_u32_right(t.min, 10);
        fmt_u32_right(t.max, 10);
        fmt_u32_right(t.count, 9);
        fmt_str(CON, "\r\n");
    }
    fmt_str(CON, "PB12 is high during 'sensor read'. 'timing reset' clears the table.\r\n");
}

/* power        shows the mode and how much of the time the chip was in STOP
   power MODE   run / sleep / stop (for the current measurements) */
static void cmd_power(const char *arg)
{
    uint32_t up_ms = power_stats_ms();
    uint32_t stop_ms = power_stop_ms();
    (void)arg;

    fmt_str(CON, "Power mode: ");
    fmt_str(CON, power_mode_name(power_mode()));
    fmt_str(CON, "\r\nSTOP: ");
    fmt_u32(CON, power_stop_count());
    fmt_str(CON, " times, ");
    fmt_u32(CON, stop_ms / 1000U);
    fmt_str(CON, " s of ");
    fmt_u32(CON, up_ms / 1000U);
    fmt_str(CON, " s since reset or 'power reset' (");
    fmt_u32(CON, up_ms > 0U ? (uint32_t)(((uint64_t)stop_ms * 100U) / up_ms) : 0U);
    fmt_str(CON, " %), woken by a key ");
    fmt_u32(CON, power_key_wakes());
    fmt_str(CON, " times\r\n");
    if (power_mode() == POWER_STOP) {
        if (!rtc_running()) {
            fmt_str(CON, "No STOP: the RTC crystal is not running (SLEEP is used instead)\r\n");
        } else if (DHCSR & DHCSR_C_DEBUGEN) {
            fmt_str(CON, "No STOP: a debugger is attached (power the board off and on)\r\n");
        } else {
            fmt_str(CON, "STOP starts 30 s after the last key. Press Enter first to wake the console.\r\n");
        }
    }
}

static void cmd_power_set(const char *arg)
{
    power_mode_t m;
    int32_t counts;

    if (str_equal(arg, "reset")) {
        power_stats_reset();
        fmt_str(CON, "OK STOP counters cleared. Wait without typing, then press Enter and type power\r\n");
        return;
    }
    if (str_equal(arg, "test")) {
        fmt_str(CON, "STOP for 1 second, the RTC alarm must wake it...\r\n");
        uart_flush();
        counts = power_stop_test();
        if (counts < 0) {
            fmt_str(CON, "ERR could not set the RTC alarm (is the clock crystal running?)\r\n");
        } else {
            fmt_str(CON, "Woke up after ");
            fmt_u32(CON, (uint32_t)counts);
            fmt_str(CON, " RTC counts (1024 = 1 s): STOP and the alarm work.\r\n");
        }
        return;
    }
    if (!power_parse_mode(arg, &m)) {
        fmt_str(CON, "ERR use: power run / sleep / stop / test / reset\r\n");
        return;
    }
    power_set_mode(m);
    logger_led_update();
    fmt_str(CON, "OK power mode = ");
    fmt_str(CON, power_mode_name(m));
    fmt_str(CON, "\r\n");
}

/* clears the terminal screen (ANSI codes: erase screen, cursor to the top; PuTTY understands them) */
static void cmd_clc(const char *arg)
{
    (void)arg;
    fmt_str(CON, "\033[2J\033[H");
}

static void cmd_timing_reset(const char *arg)
{
    (void)arg;
    timing_reset();
    fmt_str(CON, "OK timing cleared\r\n");
}

static void cmd_help(const char *arg);

/* ---------------- debug and bring-up commands ---------------- */
#if CLI_DEBUG_CMDS

static void cmd_scan(const char *arg)
{
    uint8_t a, count = 0;
    (void)arg;
    fmt_str(CON, "Scanning...\r\n");
    for (a = 0x08; a <= 0x77; a++) {
        if (i2c_probe(a)) {
            fmt_str(CON, "Found 0x");
            fmt_hex8(CON, a);
            if (a == 0x68 || a == 0x69) {
                fmt_str(CON, " (MPU6050 range)");
            }
            if (a == 0x76 || a == 0x77) {
                fmt_str(CON, " (BMP280 range)");
            }
            fmt_str(CON, "\r\n");
            count++;
        }
    }
    if (count == 0) {
        fmt_str(CON, "No devices found\r\n");
    }
}

static void cmd_bmpread(const char *arg)
{
    int32_t t;
    uint32_t p;
    int r;
    (void)arg;
    if (!bmp_ready()) {
        bmp_init();
    }
    r = bmp_read_once(&t, &p);
    if (r == BMP_OK) {
        fmt_str(CON, "BMP280 only: Temp ");
        fmt_fixed(CON, t, 2);
        fmt_str(CON, " C, Press ");
        fmt_fixed(CON, (int32_t)p, 2);
        fmt_str(CON, " hPa\r\n");
    } else {
        fmt_str(CON, "BMP280 read error, code ");
        fmt_u32(CON, (uint32_t)r);
        fmt_str(CON, "\r\n");
    }
}

static void cmd_mpuread(const char *arg)
{
    int16_t v[7];
    int32_t acc[3], gyro[3];
    int r;
    (void)arg;
    if (!mpu_ready()) {
        sensors_mpu_init();
    }
    r = mpu_read_raw(v);
    report_mpu_restart();
    if (r == MPU_OK) {
        mpu_scale(v, acc, gyro);
        fmt_str(CON, "MPU6050 only: Acc g ");
        fmt_vec3(CON, acc, 3);
        fmt_str(CON, "  Gyro dps ");
        fmt_vec3(CON, gyro, 2);
        fmt_str(CON, "\r\n");
    } else {
        fmt_str(CON, "MPU6050 read error\r\n");
    }
}

/* reads both sensors N times and counts the errors: tells wiring problems from code problems */
static void cmd_i2ctest(const char *arg)
{
    uint32_t count, i, bmp_good = 0, mpu_good = 0, mpu_bad = 0;
    uint32_t bmp_err[BMP_ERR_COUNT] = { 0 };
    uint32_t p;
    int32_t t, tmin = 100000, tmax = -100000;
    int16_t v[7];
    int r;

    if (!parse_u32(arg, &count, 200U) || count < 1U) {
        fmt_str(CON, "ERR use: i2ctest N  (N from 1 to 200)\r\n");
        return;
    }
    fmt_str(CON, "Testing both sensors, please wait...\r\n");
    for (i = 0; i < count; i++) {
        r = bmp_read_once(&t, &p);
        if (r == BMP_OK) {
            bmp_good++;
            if (t < tmin) { tmin = t; }
            if (t > tmax) { tmax = t; }
        } else if (r < BMP_ERR_COUNT) {
            bmp_err[r]++;
        }
        if (mpu_read_raw(v) == MPU_OK) {
            mpu_good++;
        } else {
            mpu_bad++;
        }
        report_mpu_restart();
        wait_ms(20);
    }
    fmt_str(CON, "BMP280: ");
    fmt_u32(CON, bmp_good);
    fmt_str(CON, " ok, errors: code1=");
    fmt_u32(CON, bmp_err[BMP_ERR_START]);
    fmt_str(CON, " code3=");
    fmt_u32(CON, bmp_err[BMP_ERR_READ]);
    fmt_str(CON, " code4=");
    fmt_u32(CON, bmp_err[BMP_ERR_NODATA]);
    fmt_str(CON, " code5=");
    fmt_u32(CON, bmp_err[BMP_ERR_RANGE]);
    fmt_str(CON, "\r\nMPU6050: ");
    fmt_u32(CON, mpu_good);
    fmt_str(CON, " ok, ");
    fmt_u32(CON, mpu_bad);
    fmt_str(CON, " failed\r\n");
    if (bmp_good > 0U) {
        fmt_str(CON, "BMP280 temperature seen: ");
        fmt_fixed(CON, tmin, 2);
        fmt_str(CON, " .. ");
        fmt_fixed(CON, tmax, 2);
        fmt_str(CON, " C\r\n");
    }
}

/* The chip's temperature sensor has an offset that differs from chip to chip.
   Run this right after a cold start (power off for 15 minutes, then power on and type tscal
   within 30 seconds), when the chip has not warmed itself yet. */
static void cmd_tscal(const char *arg)
{
    int32_t vdd, raw, bt, offset;
    uint32_t bp;
    (void)arg;

    if (!adc_read_chip(&vdd, &raw)) {
        fmt_str(CON, "ERR ADC is not running\r\n");
        return;
    }
    if (!bmp_ready()) {
        bmp_init();
    }
    if (bmp_read_once(&bt, &bp) != BMP_OK) {
        fmt_str(CON, "ERR BMP280 not ready\r\n");
        return;
    }
    offset = bt - raw;
    if (offset > 4000 || offset < -4000) {
        fmt_str(CON, "ERR offset larger than 40 C, something is wrong\r\n");
        return;
    }
    adc_set_ts_offset(offset);
    fmt_str(CON, "OK. Chip sensor read ");
    fmt_fixed(CON, raw, 2);
    fmt_str(CON, " C, BMP280 reads ");
    fmt_fixed(CON, bt, 2);
    fmt_str(CON, " C, offset = ");
    fmt_i32(CON, offset);
    fmt_str(CON, " (hundredths of a degree)\r\nTo keep it after a reset, put this in config.h:\r\n#define TS_OFFSET_C100 ");
    fmt_i32(CON, offset);
    fmt_str(CON, "\r\n");
}

/* "3.22" or "3.2" or "3" -> 3220 mV. 0 = not a voltage. */
static uint32_t parse_mv(const char *s)
{
    uint32_t v = 0, frac = 0, scale = 1000;
    for (; *s >= '0' && *s <= '9'; s++) {
        v = v * 10U + (uint32_t)(*s - '0');
        if (v > 9U) {
            return 0;
        }
    }
    if (*s == '.') {
        for (s++; *s >= '0' && *s <= '9' && scale > 1U; s++) {
            scale /= 10U;
            frac += (uint32_t)(*s - '0') * scale;
        }
    }
    if (*s != '\0') {
        return 0;
    }
    return v * 1000U + frac;
}

/* The internal 1.20 V reference differs from chip to chip, so the VDD reading can be a few
   percent off. Measure the 3V3 pin with a multimeter and type: vddcal 3.22 */
static void cmd_vddcal(const char *arg)
{
    int32_t vdd, raw, vref;
    uint32_t real = parse_mv(arg);

    if (real < 2000U || real > 3600U) {
        fmt_str(CON, "ERR give the multimeter value of the 3V3 pin, like: vddcal 3.22\r\n");
        return;
    }
    if (!adc_read_chip(&vdd, &raw)) {
        fmt_str(CON, "ERR ADC is not running\r\n");
        return;
    }
    vref = (adc_vref_x10() * (int32_t)real + vdd / 2) / vdd;      /* at most 12500 * 3600: fits */
    if (vref < 11500 || vref > 12500) {
        fmt_str(CON, "ERR that is more than 4 % away from the chip's reading, check the meter\r\n");
        return;
    }
    adc_set_vref_x10(vref);
    fmt_str(CON, "OK. Chip read ");
    fmt_fixed(CON, (vdd + 5) / 10, 2);              /* rounded, not cut off */
    fmt_str(CON, " V, meter ");
    fmt_fixed(CON, (int32_t)real / 10, 2);
    fmt_str(CON, " V, reference = ");
    fmt_fixed(CON, vref, 4);
    fmt_str(CON, " V\r\nTo keep it after a reset, put this in config.h:\r\n#define VREF_X10 ");
    fmt_i32(CON, vref);
    fmt_str(CON, "\r\n");
}

/* checks the BMP280 formulas against the example in the Bosch datasheet */
static void cmd_selftest(const char *arg)
{
    int32_t  t;
    uint32_t p;
    (void)arg;

    bmp_selftest(&t, &p);
    fmt_str(CON, "Temp = ");
    fmt_fixed(CON, t, 2);
    fmt_str(CON, " C (expect 25.08), Pressure = ");
    fmt_fixed(CON, (int32_t)p, 2);
    fmt_str(CON, " hPa (expect 1006.53)\r\n");
}

/* test of the MPU power-loss recovery without pulling wires */
static void cmd_mpusleep(const char *arg)
{
    (void)arg;
    mpu_force_sleep();
    fmt_str(CON, "MPU6050 reset and asleep. The next read must say it was restarted.\r\n");
}

static void cmd_crctest(const char *arg)
{
    (void)arg;
    fmt_str(CON, "CRC-32 of 123456789 = ");
    fmt_hex32(CON, crc32_str("123456789"));
    fmt_str(CON, " (expect CBF43926)\r\n");
}

static void sd_trace_print(const char *name, uint32_t arg, uint8_t r1)
{
    fmt_str(CON, "  ");
    fmt_str(CON, name);
    fmt_str(CON, " arg=");
    fmt_hex32(CON, arg);
    fmt_str(CON, " -> 0x");
    fmt_hex8(CON, r1);
    fmt_str(CON, "\r\n");
}

static void cmd_sd_trace_on(const char *arg)  { (void)arg; sd_set_trace(sd_trace_print); fmt_str(CON, "OK trace on\r\n"); }
static void cmd_sd_trace_off(const char *arg) { (void)arg; sd_set_trace(0); fmt_str(CON, "OK trace off\r\n"); }

/* prints 64 bytes starting at offset, 16 per line, with the text next to them */
static void hexdump64(const uint8_t *b, uint16_t offset)
{
    uint16_t i, j;
    for (i = 0; i < 64U; i += 16U) {
        fmt_hex8(CON, (uint8_t)((offset + i) >> 8));
        fmt_hex8(CON, (uint8_t)(offset + i));
        fmt_str(CON, ": ");
        for (j = 0; j < 16U; j++) {
            fmt_hex8(CON, b[offset + i + j]);
            fmt_char(CON, ' ');
        }
        fmt_char(CON, ' ');
        for (j = 0; j < 16U; j++) {
            char c = (char)b[offset + i + j];
            fmt_char(CON, (c >= 32 && c < 127) ? c : '.');
        }
        fmt_str(CON, "\r\n");
    }
}

static void cmd_sd_read(const char *arg)
{
    static uint8_t sector_buf[SD_BLOCK_SIZE];
    uint32_t sector;
    int r;

    if (!parse_u32(arg, &sector, 0xFFFFFFFFU)) {
        fmt_str(CON, "ERR use: sd read N   (N = sector number, 0 = first sector)\r\n");
        return;
    }
    r = sd_read_block(sector, sector_buf);
    if (r != SD_OK) {
        fmt_str(CON, "ERR ");
        fmt_u32(CON, (uint32_t)r);
        fmt_str(CON, ": ");
        fmt_str(CON, sd_error_text(r));
        fmt_str(CON, "\r\n");
        return;
    }
    fmt_str(CON, "Sector ");
    fmt_u32(CON, sector);
    fmt_str(CON, ", first 64 bytes:\r\n");
    hexdump64(sector_buf, 0);
    fmt_str(CON, "last 64 bytes:\r\n");
    hexdump64(sector_buf, 448);
}

#endif /* CLI_DEBUG_CMDS */

/* ---------------- the command table ---------------- */

#define L_NONE 0U
#define L_I2C  1U                       /* the command uses the sensors */
#define L_SD   2U                       /* the command uses the SD card */

typedef struct {
    const char *name;                   /* ends in a space = takes an argument */
    void      (*run)(const char *arg);
    uint8_t     locks;                  /* taken by handle_line while the command runs */
    const char *usage;                  /* how it looks when typed */
    const char *help;
} command_t;

static const command_t commands[] = {
    { "start",        cmd_start,         L_NONE, "start / stop",          "log one line per rate, LED on while logging" },
    { "stop",         cmd_stop,          L_NONE, 0,                       0 },
    { "set rate ",    cmd_set_rate,      L_NONE, "set rate N",            "N = 250..60000 ms between lines" },
    { "read",         cmd_read,          L_I2C,  "read",                  "one readable line from all sensors" },
    { "set time ",    cmd_set_time,      L_NONE, "set time YYYY-MM-DD HH:MM:SS", "set the clock" },
    { "time",         cmd_time,          L_NONE, "time",                  "show the clock" },
    { "zero",         cmd_zero,          L_I2C,  "zero",                  "re-zero the gyro (keep still)" },
    { "chip",         cmd_chip,          L_I2C,  "chip",                  "chip temperature and supply voltage" },
    { "status",       cmd_status,        L_SD,   "status",                "logger state and error counters" },
    { "sd mount",     cmd_sd_mount,      L_SD,   "sd mount",              "mount the card (FatFs)" },
    { "ls",           cmd_ls,            L_SD,   "ls",                    "list the files on the card" },
    { "cat ",         cmd_cat,           L_NONE, "cat FILE",              "print a file (any key stops it)" },
    { "sd init",      cmd_sd_init,       L_SD,   "sd init",               "start the SD card, print type and size" },
#if CLI_DEBUG_CMDS
    { "scan",         cmd_scan,          L_I2C,  "scan",                  "list the I2C devices that answer" },
    { "bmpread",      cmd_bmpread,       L_I2C,  "bmpread / mpuread",     "read just one sensor" },
    { "mpuread",      cmd_mpuread,       L_I2C,  0,                       0 },
    { "i2ctest ",     cmd_i2ctest,       L_I2C,  "i2ctest N",             "N reads of both sensors, counts the errors" },
    { "tscal",        cmd_tscal,         L_I2C,  "tscal",                 "calibrate the chip sensor against the BMP280" },
    { "vddcal ",      cmd_vddcal,        L_I2C,  "vddcal V",              "correct VDD with a meter reading of 3V3, like vddcal 3.22" },
    { "selftest",     cmd_selftest,      L_I2C,  "selftest",              "check the BMP280 formulas (datasheet example)" },
    { "mpusleep",     cmd_mpusleep,      L_I2C,  "mpusleep",              "reset the MPU6050 (tests the recovery)" },
    { "crctest",      cmd_crctest,       L_NONE, "crctest",               "check the CRC-32 (expect CBF43926)" },
    { "sd read ",     cmd_sd_read,       L_SD,   "sd read N",             "show sector N of the SD card" },
    { "sd trace on",  cmd_sd_trace_on,   L_SD,   "sd trace on / off",     "show every SD start-up command" },
    { "sd trace off", cmd_sd_trace_off,  L_SD,   0,                       0 },
#endif
    { "tasks",        cmd_tasks,         L_NONE, "tasks",                 "FreeRTOS tasks: state, free stack, CPU use" },
    { "power",        cmd_power,         L_NONE, "power",                 "low-power mode and time spent in STOP" },
    { "power ",       cmd_power_set,     L_NONE, "power run|sleep|stop",  "choose the low-power mode (also: power test, power reset)" },
    { "timing",       cmd_timing,        L_NONE, "timing / timing reset", "how long each step takes (DWT counter)" },
    { "timing reset", cmd_timing_reset,  L_NONE, 0,                       0 },
    { "clc",          cmd_clc,           L_NONE, "clc",                   "clear the screen" },
    { "help",         cmd_help,          L_NONE, "help",                  "this list" },
};
#define NUM_COMMANDS (sizeof(commands) / sizeof(commands[0]))

static void cmd_help(const char *arg)
{
    uint8_t i, n;
    (void)arg;
    for (i = 0; i < NUM_COMMANDS; i++) {
        if (commands[i].usage == 0) {
            continue;                   /* listed together with the row above */
        }
        fmt_str(CON, commands[i].usage);
        for (n = str_len(commands[i].usage); n < 30U; n++) {
            fmt_char(CON, ' ');
        }
        fmt_char(CON, ' ');
        fmt_str(CON, commands[i].help);
        fmt_str(CON, "\r\n");
    }
}

static void run_locked(const command_t *c, const char *arg)   /* UART already held */
{
    if (c->locks & L_I2C) { lock_take(LOCK_I2C, LOCK_FOREVER); }
    if (c->locks & L_SD)  { lock_take(LOCK_SD, LOCK_FOREVER); }
    c->run(arg);
    if (c->locks & L_SD)  { lock_give(LOCK_SD); }
    if (c->locks & L_I2C) { lock_give(LOCK_I2C); }
}

static void handle_line(const char *cmd)
{
    uint8_t i, len;

    if (cmd[0] == '\0') {
        return;
    }
    for (i = 0; i < NUM_COMMANDS; i++) {
        len = str_len(commands[i].name);
        if (commands[i].name[len - 1U] == ' ') {          /* takes an argument */
            if (starts_with(cmd, commands[i].name)) {
                run_locked(&commands[i], cmd + len);
                return;
            }
        } else if (str_equal(cmd, commands[i].name)) {
            run_locked(&commands[i], "");
            return;
        }
    }
    fmt_str(CON, "ERR unknown command: '");
    fmt_safe(CON, cmd);
    fmt_str(CON, "'  (type help)\r\n");
}

/* ---------------- line editing ---------------- */

/* From the first key until Enter, CliTask keeps the UART, so log lines do not land in the
   middle of what you type (they still go to the SD card). If you stop typing for
   TYPING_IDLE_MS, the UART is given back and the log lines show again. */
#define LINE_SIZE      40U
#define TYPING_IDLE_MS 10000U
static char    line[LINE_SIZE];
static uint8_t line_len = 0;
static uint8_t typing = 0;                             /* 1 = we hold the UART lock */

void cli_poll(void)
{
    uint8_t c;

    if (!uart_getc_wait(&c, typing ? TYPING_IDLE_MS : UART_WAIT_FOREVER)) {   /* sleeps here */
        if (typing) {
            uart_unlock();                             /* stopped typing: let log lines through */
            typing = 0;
        }
        return;
    }
    if (!typing) {
        uart_lock(UART_WAIT_FOREVER);
        typing = 1;
    }
    if (c == '\r' || c == '\n') {
        fmt_str(CON, "\r\n");
        line[line_len] = '\0';
        handle_line(line);
        uart_rx_flush();                               /* forget what was typed meanwhile */
        line_len = 0;
        fmt_str(CON, "> ");
        uart_unlock();
        typing = 0;
        return;
    } else if (c == 0x08 || c == 0x7F) {               /* backspace */
        if (line_len > 0) {
            line_len--;
            fmt_str(CON, "\b \b");
        }
    } else if (line_len < LINE_SIZE - 1U) {
        line[line_len++] = (char)c;
        fmt_char(CON, (char)c);                        /* echo */
    }
    uart_flush();                                      /* show the echo now */
}
