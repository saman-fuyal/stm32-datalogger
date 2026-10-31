/*
 * Settings you may want to change. Everything else lives in the modules.
 */
#ifndef CONFIG_H
#define CONFIG_H

/* debug and bring-up commands (scan, i2ctest, bmpread, mpuread, selftest, crctest, tscal,
   sd read). 1 = included, 0 = left out. 'make RELEASE=1' builds with 0. */
#ifndef CLI_DEBUG_CMDS
#define CLI_DEBUG_CMDS 1
#endif

/* serial port: 0x341 = 9600 baud, 0x45 = 115200 baud (8 MHz clock).
   115200: a log line takes 7 ms to send instead of 89 ms, and the chip cannot enter STOP
   while text is still going out, so this gives more time in STOP. */
#define UART_BRR_VALUE  0x45U

/* I2C addresses */
#define BMP_ADDR        0x76U
#define MPU_ADDR        0x68U

/* BMP280 measurement: 0x25 = temperature x1, pressure x1, forced mode (measure once).
   0x55 = temperature x2, pressure x16 (less noise, but then set BMP_WAIT_MS to 50). */
#define BMP_CTRL_FORCED 0x25U
#define BMP_WAIT_MS     15U

/* accelerometer calibration: paste the lines printed by 'cal done' (bringup/08) */
#define ACC_OFF_X   653
#define ACC_OFF_Y   46
#define ACC_OFF_Z   925
#define ACC_SCALE_X 16375
#define ACC_SCALE_Y 16356
#define ACC_SCALE_Z 16510

/* offset of the chip's internal temperature sensor, hundredths of a degree ('tscal') */
#define TS_OFFSET_C100  0

/* the chip's internal reference in tenths of a millivolt: 1.20 V by the datasheet, really
   1.16..1.24 V. 'vddcal' measures it against a multimeter on the 3V3 pin. */
#define VREF_X10        11695

/* logging rate limits in milliseconds */
#define RATE_MS_DEFAULT 1000U
#define RATE_MS_MIN     250U
#define RATE_MS_MAX     60000U

/* 1 = start logging by itself after power-up (the product behaviour), 0 = wait for 'start' */
#define AUTO_START      1

/* while logging without a card: try to mount it again this often */
#define SD_RETRY_MS     5000U

/* low power (power.c). Mode at power-on: POWER_RUN (no sleep), POWER_SLEEP (CPU waits with
   WFI), POWER_STOP (STOP mode between samples, the product setting). 'power' changes it. */
#define POWER_MODE_DEFAULT POWER_STOP
#define STOP_MIN_MS      10U     /* shorter idle times use SLEEP: STOP has a fixed cost */
#define STOP_MAX_MS      5000U   /* wake up at least this often, even if no task needs it */
#define STOP_HOLDOFF_MS  10000U  /* no STOP in the first 10 s after reset (time to flash) */
#define CONSOLE_AWAKE_MS 30000U  /* after a key: no STOP for 30 s, so the UART can receive */

#endif
