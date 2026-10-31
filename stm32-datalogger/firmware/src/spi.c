/* SPI1 on PA5 (SCK), PA6 (MISO), PA7 (MOSI); CS on PA4. Pins are set up in board.c. */
#include "board.h"
#include "spi.h"

void spi_set_speed(uint8_t br)
{
    uint32_t t = 100000U;
    while ((SPI1_SR & SPI_SR_BSY) && --t) { }
    SPI1_CR1 = 0;                                                 /* off, so BR can change */
    SPI1_CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI | ((uint32_t)br << 3);
    SPI1_CR1 |= SPI_CR1_SPE;
}

uint8_t spi_xfer(uint8_t out)
{
    uint32_t t = 100000U;
    while (!(SPI1_SR & SPI_SR_TXE)) {
        if (--t == 0) { return 0xFF; }
    }
    SPI1_DR = out;
    t = 100000U;
    while (!(SPI1_SR & SPI_SR_RXNE)) {
        if (--t == 0) { return 0xFF; }
    }
    return (uint8_t)SPI1_DR;
}

void spi_cs_high(void) { GPIOA_BSRR = (1U << SD_CS_PIN); }
void spi_cs_low(void)  { GPIOA_BSRR = (1U << (SD_CS_PIN + 16U)); }
