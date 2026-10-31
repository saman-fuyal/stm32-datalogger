/*
 * Datalogger: BMP280 (temperature, pressure) + MPU6050 (acceleration, rotation)
 *             + the chip's own temperature sensor and supply voltage (ADC with DMA)
 * Board: STM32F103C8T6 (Blue Pill), 8 MHz internal clock, registers only (no HAL).
 * Runs on FreeRTOS: see app.h for the tasks.
 *
 * main() only sets up what needs no waiting (clocks, pins, UART, I2C, SPI, the mutexes,
 * the wake-up lines),
 * then starts FreeRTOS. Everything else starts in the start-up task (app.c).
 * Type 'help' in the terminal to see the commands.
 */
#include "app.h"
#include "board.h"
#include "i2c.h"
#include "locks.h"
#include "power.h"
#include "spi.h"
#include "uart.h"

int main(void)
{
    board_init();
    locks_init();
    power_init();
    uart_init();
    i2c_init();
    spi_set_speed(SPI_BR_SLOW);
    app_start();                        /* never returns */
    return 0;
}
