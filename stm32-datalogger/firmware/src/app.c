/* The tasks (see app.h), the start-up checks, and what happens when something goes wrong. */
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "adc.h"
#include "app.h"
#include "board.h"
#include "bmp280.h"
#include "cli.h"
#include "config.h"
#include "fmt.h"
#include "locks.h"
#include "logger.h"
#include "mpu6050.h"
#include "power.h"
#include "rtc.h"
#include "timing.h"
#include "uart.h"

/* stack sizes in words (4 bytes). 'tasks' shows how much of each is still unused. */
#define STACK_START   512U
#define STACK_SENSOR  256U
#define STACK_LOGGER  384U
#define STACK_CLI     512U

#define PRIO_CLI      1U
#define PRIO_LOGGER   2U
#define PRIO_SENSOR   3U
#define PRIO_START    4U

#define QUEUE_LEN     8U        /* records waiting for the SD card: 8 s at the 1 s rate */

static QueueHandle_t records;
static TaskHandle_t  sensor_task = NULL;
static uint32_t      dropped = 0;

static void panic(uint16_t code, const char *what, const char *detail);

/* codes kept in BKP_DR4 across the restart, so the next start-up can tell what happened */
#define FAULT_NONE      0U
#define FAULT_STACK     0xF001U
#define FAULT_HEAP      0xF002U
#define FAULT_ASSERT    0xF003U
#define FAULT_HARD      0xF004U
#define FAULT_OTHER     0xF005U

/* prints why the chip restarted (RCC_CSR flags), then clears the flags */
static void print_reset_cause(void)
{
    uint32_t csr = RCC_CSR;
    uint16_t fault;

    RCC_APB1ENR |= RCC_APB1ENR_PWREN | RCC_APB1ENR_BKPEN;   /* backup registers readable, */
    PWR_CR |= PWR_CR_DBP;                                    /* and writable */
    fault = (uint16_t)BKP_DR4;

    fmt_str(CON, "Last reset: ");
    if (csr & RCC_CSR_PORRSTF) {
        fmt_str(CON, "power-on, or the supply voltage dropped too low");
    } else if (csr & RCC_CSR_SFTRSTF) {
        fmt_str(CON, fault == FAULT_STACK  ? "firmware restart after a stack overflow" :
                     fault == FAULT_HEAP   ? "firmware restart, out of heap" :
                     fault == FAULT_ASSERT ? "firmware restart after a failed check (assert)" :
                     fault == FAULT_HARD   ? "firmware restart after a hard fault" :
                                             "software reset (normal after flashing with the ST-Link)");
        if (fault == FAULT_HARD) {
            fmt_str(CON, "\r\n            at PC 0x");
            fmt_hex32(CON, ((BKP_DR5 & 0xFFFFU) << 16) | (BKP_DR6 & 0xFFFFU));
            fmt_str(CON, ", LR 0x");
            fmt_hex32(CON, ((BKP_DR7 & 0xFFFFU) << 16) | (BKP_DR8 & 0xFFFFU));
            fmt_str(CON, "\r\n            CFSR 0x");
            fmt_hex32(CON, BKP_DR9 & 0xFFFFU);
            fmt_str(CON, ", flags 0x");
            fmt_hex8(CON, (uint8_t)BKP_DR10);
            fmt_str(CON, (BKP_DR10 & 16U) ? ", in task '" : ", in an interrupt '");
            fmt_char(CON, (char)((BKP_DR10 >> 8) & 0xFFU));
            fmt_str(CON, "'");
            if (BKP_DR10 & 4U) {
                fmt_str(CON, "\r\n            (a debugger breakpoint was hit without a debugger: harmless after flashing)");
            }
        }
    } else if (csr & (RCC_CSR_IWDGRSTF | RCC_CSR_WWDGRSTF)) {
        fmt_str(CON, "watchdog (the chip was stuck). Using SLEEP instead of STOP until power-off;\r\n"
                     "            'power stop' turns STOP back on");
        power_set_mode(POWER_SLEEP);              /* fallback: do not risk the same hang again */
    } else if (csr & RCC_CSR_LPWRRSTF) {
        fmt_str(CON, "low-power reset");
    } else if (csr & RCC_CSR_PINRSTF) {
        fmt_str(CON, "reset pin (button, ST-Link, or noise on NRST)");
    } else {
        fmt_str(CON, "unknown");
    }
    fmt_str(CON, "\r\n");
    RCC_CSR |= RCC_CSR_RMVF;
    BKP_DR4 = FAULT_NONE;
}

/* ---------------- SensorTask ---------------- */

static void sensor_task_fn(void *arg)
{
    record_t r;
    TickType_t next, now;
    uint32_t last_start = 0, t0;
    uint8_t have_last = 0;
    (void)arg;

    (void)ulTaskNotifyTake(pdTRUE, 0);            /* forget a wake-up sent before we ran */
    next = xTaskGetTickCount();
    for (;;) {
        if (!logger_running()) {
            (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  /* sleep until 'start' */
            next = xTaskGetTickCount();
            have_last = 0;
            continue;
        }

        t0 = timing_now_us();
        if (have_last) {
            timing_record(TM_PERIOD, t0 - last_start);
        }
        last_start = t0;
        have_last = 1;

        debug_pin(1);
        logger_led_pulse(1);
        if (logger_take_record(&r)) {
            if (xQueueSend(records, &r, 0) != pdTRUE) {
                dropped++;                        /* LoggerTask is far behind: lose this one */
            }
        }
        logger_led_pulse(0);
        debug_pin(0);
        timing_record(TM_SENSOR, timing_now_us() - t0);

        /* the next sample is exactly one period after the last planned one, so the rate
           does not drift. If we are already late, take the next one at once. */
        next += pdMS_TO_TICKS(logger_rate());
        now = xTaskGetTickCount();
        if ((int32_t)(next - now) <= 0) {
            next = now;
        }
        if (ulTaskNotifyTake(pdTRUE, next - now) != 0U) {   /* woken early: start/stop/rate */
            next = xTaskGetTickCount();
            have_last = 0;
        }
    }
}

/* ---------------- LoggerTask ---------------- */

static void logger_task_fn(void *arg)
{
    record_t r;
    uint32_t wait;
    (void)arg;
    for (;;) {
        /* sleep until a record comes or housekeeping has work: no needless wake-ups,
           so the chip can stay in STOP mode */
        wait = logger_wait_ms();
        if (xQueueReceive(records, &r, wait == LOGGER_WAIT_FOREVER ? portMAX_DELAY
                                                                    : pdMS_TO_TICKS(wait)) == pdTRUE &&
            logger_running()) {
            logger_write_record(&r);
        }
        logger_housekeeping();
    }
}

/* ---------------- CliTask ---------------- */

static void cli_task_fn(void *arg)
{
    (void)arg;
    uart_rx_flush();                              /* junk received while starting up */
    uart_lock(UART_WAIT_FOREVER);
    fmt_str(CON, "> ");
    uart_unlock();
    for (;;) {
        cli_poll();                               /* sleeps until a key arrives */
    }
}

/* ---------------- start-up task ---------------- */

static void make_task(TaskFunction_t fn, const char *name, uint16_t stack, UBaseType_t prio,
                      TaskHandle_t *handle)
{
    if (xTaskCreate(fn, name, stack, NULL, prio, handle) != pdPASS) {
        panic(FAULT_OTHER, "cannot create task ", name);
    }
}

/* The driver start-up is here, not in main(), because it waits (crystal, sensors, gyro
   zero) and waiting needs the FreeRTOS tick, which only runs once the scheduler started. */
static void start_task_fn(void *arg)
{
    int ok;
    (void)arg;

    uart_lock(UART_WAIT_FOREVER);
    fmt_str(CON, "\r\nDatalogger (BMP280 + MPU6050) on FreeRTOS " tskKERNEL_VERSION_NUMBER
                 ". Type help for commands.\r\n");
    print_reset_cause();
#ifndef __OPTIMIZE__
    fmt_str(CON, "WARNING: built without optimisation (-O0). Set -Os in CubeIDE: the code is twice as\r\n"
                 "         big and the task stacks may overflow.\r\n");
#endif
    if (rtc_needs_setup()) {
        fmt_str(CON, "Starting the 32 kHz clock crystal (can take a few seconds)...\r\n");
    }
    if (!rtc_init()) {
        fmt_str(CON, "RTC crystal did not start within 10 s (software clock, SLEEP instead of STOP)\r\n");
    } else {
        if (rtc_start_ms() > 0U) {
            fmt_str(CON, "Crystal started in ");
            fmt_u32(CON, rtc_start_ms());
            fmt_str(CON, " ms\r\n");
        }
        fmt_str(CON, "Time: ");
        fmt_datetime(CON, rtc_get());
        fmt_str(CON, "\r\n");
        if (!rtc_clock_is_set()) {
            fmt_str(CON, "Clock not set. Use: set time YYYY-MM-DD HH:MM:SS\r\n");
        }
    }

    lock_take(LOCK_I2C, LOCK_FOREVER);
    fmt_str(CON, bmp_init() ? "BMP280 ready\r\n" : "BMP280 NOT found, check wiring\r\n");
    if (sensors_mpu_init()) {
        fmt_str(CON, "MPU6050 ready. Zeroing gyro, keep it still for 1 second...\r\n");
        uart_unlock();                            /* show this line before the 1 s wait */
        ok = gyro_zero();
        uart_lock(UART_WAIT_FOREVER);
        fmt_str(CON, ok ? "Gyro zeroed\r\n" : "Gyro zero failed\r\n");
    } else {
        fmt_str(CON, "MPU6050 NOT found, check wiring\r\n");
    }
    lock_give(LOCK_I2C);

    fmt_str(CON, adc_init() ? "ADC + DMA ready (chip temperature, supply voltage)\r\n"
                            : "ADC failed to start\r\n");
    uart_unlock();

    make_task(sensor_task_fn, "Sensor", STACK_SENSOR, PRIO_SENSOR, &sensor_task);
    make_task(logger_task_fn, "Logger", STACK_LOGGER, PRIO_LOGGER, NULL);
#if AUTO_START
    logger_start();                               /* the product behaviour: switch on, it logs */
#endif
    make_task(cli_task_fn, "Cli", STACK_CLI, PRIO_CLI, NULL);
#if CLI_DEBUG_CMDS
    if (uart_lock(100U)) {
        fmt_str(CON, "(start-up task: ");
        fmt_u32(CON, (uint32_t)uxTaskGetStackHighWaterMark(NULL) * 4U);
        fmt_str(CON, " of " );
        fmt_u32(CON, STACK_START * 4U);
        fmt_str(CON, " stack bytes never used)\r\n");
        uart_unlock();
    }
#endif
    vTaskDelete(NULL);                            /* done; the idle task frees this stack */
}

void app_start(void)
{
    records = xQueueCreate(QUEUE_LEN, sizeof(record_t));
    if (records == NULL) {
        panic(FAULT_OTHER, "cannot create the queue", "");
    }
    make_task(start_task_fn, "Start", STACK_START, PRIO_START, NULL);
    vTaskStartScheduler();                        /* never comes back if it worked */
    panic(FAULT_HEAP, "scheduler did not start (heap too small?)", "");
}

void app_wake_sensor(void)
{
    if (sensor_task != NULL) {
        xTaskNotifyGive(sensor_task);
    }
}

void app_flush_records(void)
{
    (void)xQueueReset(records);
}

uint32_t app_dropped_records(void)
{
    return dropped;
}

/* ---------------- when something goes wrong ----------------
   Print the reason without interrupts or DMA, then restart the chip. A data logger that
   restarts and keeps logging is better than one that hangs. */

static void panic(uint16_t code, const char *what, const char *detail)
{
    volatile uint32_t n;
    __asm volatile ("cpsid i" ::: "memory");      /* nothing else may run now */
    RCC_APB1ENR |= RCC_APB1ENR_PWREN | RCC_APB1ENR_BKPEN;
    PWR_CR |= PWR_CR_DBP;
    BKP_DR4 = code;                               /* the next start-up prints the reason */
    uart_panic_puts("\r\n*** ");
    uart_panic_puts(what);
    uart_panic_puts(detail);
    uart_panic_puts(", restarting\r\n");
    for (n = 0; n < 20000U; n++) { }              /* let the last byte leave the UART */
    SCB_AIRCR = SCB_AIRCR_RESET;
    for (;;) { }
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    panic(FAULT_STACK, "stack overflow in task ", name);
}

void vApplicationMallocFailedHook(void)
{
    panic(FAULT_HEAP, "out of FreeRTOS heap", "");
}

void app_assert_failed(const char *file, int line)
{
    char buf[12];
    outbuf_t o;
    ob_init(&o, buf, sizeof(buf));
    fmt_char(&o, ':');
    fmt_u32(&o, (uint32_t)line);
    uart_panic_puts("\r\n*** assert failed in ");
    uart_panic_puts(file);
    panic(FAULT_ASSERT, buf, "");
}

/* The CPU saved r0-r3, r12, LR, PC, xPSR on the stack that was in use (MSP or PSP).
   PC is the instruction that faulted: it is kept in backup registers for the next start-up. */
void hard_fault_report(const uint32_t *frame, uint32_t exc_return) __attribute__((used));
void hard_fault_report(const uint32_t *frame, uint32_t exc_return)
{
    char buf[48];
    outbuf_t o;
    uint32_t pc = 0xFFFFFFFFU, lr = 0xFFFFFFFFU;
    const char *task = pcTaskGetName(NULL);
    int on_psp = (exc_return & 4U) != 0U;          /* 1 = a task was running, 0 = an interrupt */

    /* if the stack pointer itself is outside RAM, the saved registers cannot be read:
       that is the sign of a task stack that overflowed or got overwritten */
    if ((uint32_t)frame >= 0x20000000U && (uint32_t)frame <= (0x20005000U - 32U)) {
        pc = frame[6];
        lr = frame[5];
    }

    RCC_APB1ENR |= RCC_APB1ENR_PWREN | RCC_APB1ENR_BKPEN;
    PWR_CR |= PWR_CR_DBP;
    BKP_DR5 = pc >> 16;
    BKP_DR6 = pc & 0xFFFFU;
    BKP_DR7 = lr >> 16;
    BKP_DR8 = lr & 0xFFFFU;
    BKP_DR9 = SCB_CFSR & 0xFFFFU;
    /* DR10: bit 0 VECTTBL, 1 FORCED, 2 DEBUGEVT, 3 debugger attached, 4 task stack; 8..15 task letter */
    BKP_DR10 = ((SCB_HFSR & HFSR_VECTTBL)  ? 1U : 0U) | ((SCB_HFSR & HFSR_FORCED) ? 2U : 0U) |
               ((SCB_HFSR & HFSR_DEBUGEVT) ? 4U : 0U) | ((DHCSR & DHCSR_C_DEBUGEN) ? 8U : 0U) |
               (on_psp ? 16U : 0U) | ((uint32_t)(uint8_t)(on_psp && task ? task[0] : '-') << 8);
    ob_init(&o, buf, sizeof(buf));
    fmt_str(&o, " at PC 0x");
    fmt_hex32(&o, pc);
    fmt_str(&o, ", CFSR 0x");
    fmt_hex32(&o, SCB_CFSR);
    panic(FAULT_HARD, "hard fault", buf);
}

void HardFault_Handler(void) __attribute__((naked));
void HardFault_Handler(void)
{
    __asm volatile (
        "tst lr, #4          \n"    /* which stack was in use: bit 2 of LR */
        "ite eq              \n"
        "mrseq r0, msp       \n"
        "mrsne r0, psp       \n"
        "mov r1, lr          \n"    /* EXC_RETURN, says which stack */
        "b hard_fault_report \n");
}
