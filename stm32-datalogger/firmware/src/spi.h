/* SPI1 master, mode 0, 8 bit. Chip select (PA4) is driven by hand. */
#ifndef SPI_H
#define SPI_H

#include <stdint.h>

/* SPI clock = 8 MHz / 2^(n+1). The SD card must be started at 100..400 kHz. */
#define SPI_BR_SLOW 4U      /* 8 MHz / 32 = 250 kHz */
#define SPI_BR_FAST 1U      /* 8 MHz / 4  = 2 MHz */

void    spi_set_speed(uint8_t br);
uint8_t spi_xfer(uint8_t out);      /* sends one byte, returns the byte received */
void    spi_cs_high(void);
void    spi_cs_low(void);

#endif
