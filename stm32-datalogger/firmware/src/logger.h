/*
 * The logger: one sample from all sensors, the readable 'read' line, the CSV log line,
 * and start / stop. The tasks in app.c call these functions:
 *   SensorTask  logger_take_record()   every 'rate' ms while logging
 *   LoggerTask  logger_write_record()  for every record from the queue, then logger_housekeeping()
 *   CliTask     logger_start(), logger_stop(), logger_set_rate(), print_sample_text()
 */
#ifndef LOGGER_H
#define LOGGER_H

#include <stdint.h>

typedef struct {
    int32_t  temp;       /* hundredths of a degree C */
    uint32_t press;      /* Pa = hundredths of a hPa */
    int32_t  acc[3];     /* thousandths of a g */
    int32_t  gyro[3];    /* hundredths of a degree per second */
    int32_t  vdd_mv;     /* supply voltage in millivolts */
    int32_t  chip_c100;  /* chip temperature in hundredths of a degree C */
    uint8_t  adc_valid;  /* 0 if the ADC values could not be read */
} sample_t;

/* what SensorTask sends to LoggerTask through the queue */
typedef struct {
    sample_t s;
    uint32_t stamp;      /* RTC seconds */
    uint32_t t_start;    /* timing_now_us() when the sample started (for the latency) */
} record_t;

#define LOG_HEADER "time,temp_C,press_hPa,ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps,vdd_V,chip_C"

/* sensor helpers; the caller holds LOCK_I2C (the CLI does that for its commands) */
int      sensors_mpu_init(void);        /* mpu_init + prints the WHO_AM_I line */
int      sensors_ensure(void);          /* sets up any sensor that is not ready. 1 = both ready */
void     report_mpu_restart(void);      /* prints a note if the MPU had to be set up again */
int      take_sample(sample_t *s);      /* prints the reason if it fails */
void     print_sample_text(void);       /* the 'read' line */

/* used by the tasks; they take the locks they need themselves */
int      logger_take_record(record_t *r);         /* 1 = r is filled */
void     logger_write_record(const record_t *r);  /* UART line + SD line */
void     logger_housekeeping(void);               /* sync, LED, card retry */
#define LOGGER_WAIT_FOREVER 0xFFFFFFFFU
uint32_t logger_wait_ms(void);                    /* when housekeeping has work next */
void     logger_led_update(void);                 /* LED for the current state and power mode */
void     logger_led_pulse(int on);                /* in STOP mode: flash during the sample */

void     logger_start(void);
void     logger_stop(void);
int      logger_running(void);
uint32_t logger_rate(void);
void     logger_set_rate(uint32_t ms);
uint32_t logger_uart_skipped(void);     /* lines not printed because the UART was busy */

#endif
