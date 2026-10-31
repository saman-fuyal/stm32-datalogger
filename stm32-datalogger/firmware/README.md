# Firmware

## Files (src/)

```
main.c           clocks, pins, buses, then starts FreeRTOS
app.c/h          tasks (Start, Sensor, Logger, Cli), startup checks, fault handling
FreeRTOSConfig.h 1 ms tick, 10 KB heap, stack check, irq priorities, tickless idle
locks.c/h        mutexes for UART, I2C, SD
power.c/h        tickless idle, STOP mode, RTC alarm + key wake-up, power command
timing.c/h       timing command, us clock, DWT run-time stats
config.h         things to change: baud, addresses, calibration, CLI_DEBUG_CMDS
board.c/h        register addresses, pins, LED, wait_ms, wait_bits
uart.c/h         USART1, RX irq -> stream buffer, TX by DMA1 ch4
fmt.c/h          text output, number parsing, CRC-32
i2c.c/h          I2C1 100 kHz with timeouts + bus recovery
rtc.c/h          RTC at 1024 Hz, base seconds in backup regs, alarm, calendar
adc.c/h          ADC1 + DMA: chip temp and VDD, on only while measuring
bmp280.c/h       BMP280 forced mode, Bosch integer formulas
mpu6050.c/h      MPU6050, scaling, gyro zero, recovers if it loses power
spi.c/h          SPI1 master
sd.c/h           SD card in SPI mode, 512 byte block read/write
diskio.c         FatFs <-> SD driver, get_fattime from RTC
sdlog.c/h        LOGnnnn.CSV files: mount, open, write, sync, close, card removal
logger.c/h       one sample, the read line, CSV line + CRC
cli.c/h          command table, help, line editing
```

`../fatfs/` is FatFs R0.16 (only ffconf.h changed), `../freertos/` is FreeRTOS kernel V11.3.1
(only the files used). Drivers don't print, they return error codes - only logger, cli and main
print.

## Build

Makefile (arm-none-eabi-gcc):

```
make              debug build (debug commands included)
make RELEASE=1    without debug commands
make flash        needs st-flash + ST-Link
```

Output: build/datalogger.elf and .bin. startup/ has the vector table and linker script for make.

STM32CubeIDE:

1. New STM32 project, STM32F103C8Tx, project type Empty.
2. Delete Core/Src/main.c.
3. Copy src/*.c, fatfs/ff.c, freertos/*.c to Core/Src and src/*.h, fatfs/ff.h, fatfs/diskio.h,
   fatfs/ffconf.h, freertos/include/*.h to Core/Inc.
4. Don't copy startup/ (CubeIDE has its own).
5. Properties > C/C++ Build > Settings > MCU GCC Compiler > Optimization: -Os.
   At -O0 it doesn't fit in 64 KB.
6. Build, flash.

Size at -Os (CubeIDE debug build): 44.8 KB flash of 64 KB, RAM 14.4 KB (10 KB is the FreeRTOS heap).

## Tasks

```
task    prio  stack   job
Start   4     2 KB    startup checks, creates the others, deletes itself
Sensor  3     1 KB    every rate ms: read sensors, put record in queue (8 long)
Logger  2     1.5 KB  record -> CSV + CRC -> UART and SD, sync, LED, card retry
Cli     1     2 KB    commands from the UART stream buffer
```

- Sensor has the highest prio so samples stay on time when the SD card is slow. Next sample
  is planned from the last planned time, so the rate doesn't drift.
- Lock order UART -> I2C -> SD. Sensor and Logger take the UART with a timeout, so when the
  CLI is printing (cat) lines still go to the card, only the UART copy is skipped.
- Mutexes, not semaphores, because of priority inheritance: if Cli holds I2C (zero takes 1 s)
  and Sensor waits for it, Cli runs at prio 3 so Logger can't get in between.
- UART TX: DMA1 ch4, two 128 byte buffers. USART1 and DMA irqs at NVIC priority 6 - anything
  calling FreeRTOS from an irq has to be 5..15, after reset they are 0.
- Stack overflow, out of heap, configASSERT and hard faults print the reason and reset.

## Low power

- configUSE_TICKLESS_IDLE 2, FreeRTOS calls power_idle(ms) when nothing is ready.
  `power run` = spin, `power sleep` = WFI, `power stop` (default) = STOP mode, RTC alarm 1 ms
  before the next task. After waking the slept time (from the crystal) goes to the tick with
  vTaskStepTick.
- RTC runs at 1024 Hz (PRL 31). seconds = base (BKP DR2/DR3) + CNT/1024, rebased every 24 days.
- Wake-up: RTC alarm = EXTI 17, key on PA10 = EXTI 10, both as events with WFE, no irq handler.
  First version used the RTCAlarm irq and never woke up on the CubeIDE build - CubeIDE calls
  vector 41 RTCAlarm_IRQHandler, not RTC_Alarm_IRQHandler, so it went to Default_Handler and
  the watchdog reset it. Found it in the vector table of the .elf. Events avoid that.
- RTC_CRH (ALRIE) is cleared by reset, so it's set again before every STOP.
- The first byte typed after a STOP is lost (no UART clock), after that the console stays
  awake 30 s.
- No STOP (SLEEP instead) when: no 32k crystal, debugger attached, first 10 s after reset
  (time to flash), 30 s after a key, UART still sending, idle < 10 ms. Max STOP 5 s.
- Watchdog (IWDG ~26 s) fed in idle. After a watchdog reset it stays in SLEEP mode until
  power off.
- Debug sessions can leave breakpoints/watchpoints on after reset -> hard fault without
  debugger. board_init clears them when no debugger is attached.
- No crystal: software clock (base + tick) so set time still works. Crystal gets 10 s to start.
  Keep PC14/PC15 out of breadboard rows, the extra capacitance can stop it.
- Hard fault: PC, LR, CFSR saved in BKP DR4-DR10, printed at next start.
- ADC (VDD, chip temp) is switched on only for each reading (reset, calibrate, 32 pairs, off).
  First version converted all the time in the background and got stuck after the first STOP:
  the values froze about 2 min after start (seen in an earlier 18 min log).

## Commands

help, status, read, chip, start, stop, set rate N, set time, time, zero, ls, cat FILE,
sd mount, sd init, tasks, timing, timing reset, power, power run|sleep|stop|test|reset, clc.
Debug builds also have scan, bmpread, mpuread, i2ctest N, tscal, vddcal V, selftest, mpusleep, crctest,
sd read N, sd trace on/off. Type `help` for the full list.
