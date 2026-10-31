# Bring-up

Test programs I wrote while getting each part working, in this order. Each folder has one
main.c - put it in an empty STM32F103C8 project instead of the generated main.c.
The final firmware grew out of these.

01_blink                   - PC13 LED, clocks and GPIO registers
02_uart_echo               - USART1 polling, prints hello and echoes keys
03_uart_rx_irq_ringbuffer  - RX interrupt + 64 byte ring buffer (115200)
04_uart_cli                - small command line, SysTick 1 ms timing
05_i2c_sensor_id           - I2C1, reads BMP280 chip ID (0x58)
06_bmp280                  - temp + pressure with the Bosch integer formulas
07_mpu6050                 - accel + gyro, scaling, gyro zero
08_mpu6050_calibration     - 6 position accel calibration, prints the #defines
09_sd_card_read            - SPI1 + SD card init in SPI mode, reads sectors (read only)

All at 9600 baud except 03.
