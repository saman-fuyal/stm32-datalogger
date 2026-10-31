/* I2C1 on PB6 (SCL) / PB7 (SDA), 100 kHz, polling, with timeouts. */
#include "board.h"
#include "i2c.h"

#define I2C_TIMEOUT_MS  25U

void i2c_init(void)
{
    I2C1_CR1 = I2C_CR1_SWRST;
    I2C1_CR1 = 0;
    I2C1_CR2 = 8U;              /* peripheral clock = 8 MHz */
    I2C1_CCR = 40U;             /* 100 kHz: 8 MHz / (2 x 100 kHz) */
    I2C1_TRISE = 9U;
    I2C1_CR1 = I2C_CR1_PE;
}

#define SCL_BIT (1U << 6)                   /* PB6 */
#define SDA_BIT (1U << 7)                   /* PB7 */

static void bit_delay(void)                 /* about 5 us at 8 MHz: one half of a 100 kHz clock */
{
    for (volatile uint8_t i = 0; i < 8U; i++) { }
}

/* A chip can be left in the middle of a byte (after a reset or a power glitch) and keep
   holding SDA low. Clocking SCL by hand up to 9 times lets it finish the byte and release
   SDA; a STOP then frees the bus. This is the standard I2C bus recovery. */
static void i2c_bus_recover(void)
{
    uint8_t i;

    I2C1_CR1 = 0;                                   /* peripheral off, pins driven by hand */
    GPIOB_BSRR = SCL_BIT | SDA_BIT;                 /* both released (high) */
    gpio_config(&GPIOB_CRL, 6, GPIO_OUT_OD_2MHZ);
    gpio_config(&GPIOB_CRL, 7, GPIO_OUT_OD_2MHZ);
    for (i = 0; i < 9U && !(GPIOB_IDR & SDA_BIT); i++) {
        GPIOB_BSRR = SCL_BIT << 16;                 /* SCL low */
        bit_delay();
        GPIOB_BSRR = SCL_BIT;                       /* SCL high */
        bit_delay();
    }
    GPIOB_BSRR = SCL_BIT << 16;                     /* STOP: SCL low, SDA low, */
    bit_delay();
    GPIOB_BSRR = SDA_BIT << 16;
    bit_delay();
    GPIOB_BSRR = SCL_BIT;                           /* then SCL high, then SDA high */
    bit_delay();
    GPIOB_BSRR = SDA_BIT;
    bit_delay();
    gpio_config(&GPIOB_CRL, 6, GPIO_AF_OD_50MHZ);   /* back to the I2C peripheral */
    gpio_config(&GPIOB_CRL, 7, GPIO_AF_OD_50MHZ);
}

static uint32_t recoveries = 0;

static int i2c_fail(void)       /* the bus got stuck: free it if needed, then reset the peripheral */
{
    if (!(GPIOB_IDR & SDA_BIT)) {
        i2c_bus_recover();
        recoveries++;
    }
    i2c_init();
    return I2C_ERR_TIMEOUT;
}

uint32_t i2c_recovery_count(void)
{
    return recoveries;
}

static int i2c_wait(uint32_t mask)         /* wait for a flag in SR1 */
{
    return wait_bits(&I2C1_SR1, mask, 1, I2C_TIMEOUT_MS);
}

static void i2c_stop(void)
{
    I2C1_CR1 |= I2C_CR1_STOP;
}

static void i2c_stop_wait(void)
{
    wait_bits(&I2C1_CR1, I2C_CR1_STOP, 0, I2C_TIMEOUT_MS);
}

static void i2c_clear_addr(void)           /* reading SR1 then SR2 clears ADDR */
{
    (void)I2C1_SR1;
    (void)I2C1_SR2;
}

/* START + address. rw: 0 = write, 1 = read. */
static int i2c_start_addr(uint8_t addr, uint8_t rw)
{
    I2C1_CR1 |= I2C_CR1_START;
    if (!i2c_wait(I2C_SR1_SB)) {
        return i2c_fail();
    }
    I2C1_DR = (uint32_t)((addr << 1) | rw);

    if (!i2c_wait(I2C_SR1_ADDR | I2C_SR1_AF)) {
        return i2c_fail();
    }
    if (I2C1_SR1 & I2C_SR1_AF) {                     /* nobody answered */
        I2C1_SR1 &= ~I2C_SR1_AF;
        i2c_stop();
        i2c_stop_wait();
        return I2C_ERR_NACK;
    }
    return I2C_OK;
}

/* sends the register number; the caller then does a repeated START to read */
static int i2c_select_reg(uint8_t addr, uint8_t reg)
{
    int r = i2c_start_addr(addr, 0);
    if (r != I2C_OK) {
        return r;
    }
    i2c_clear_addr();
    if (!i2c_wait(I2C_SR1_TXE)) { return i2c_fail(); }
    I2C1_DR = reg;
    if (!i2c_wait(I2C_SR1_BTF)) { return i2c_fail(); }
    return I2C_OK;
}

int i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *value)
{
    int r = i2c_select_reg(addr, reg);
    if (r != I2C_OK) {
        return r;
    }
    r = i2c_start_addr(addr, 1);                     /* repeated START, now read */
    if (r != I2C_OK) {
        return r;
    }
    I2C1_CR1 &= ~I2C_CR1_ACK;                        /* only one byte: answer NACK */
    i2c_clear_addr();
    i2c_stop();
    if (!i2c_wait(I2C_SR1_RXNE)) { return i2c_fail(); }
    *value = (uint8_t)I2C1_DR;
    i2c_stop_wait();
    I2C1_CR1 |= I2C_CR1_ACK;
    return I2C_OK;
}

int i2c_write_reg(uint8_t addr, uint8_t reg, uint8_t value)
{
    int r = i2c_start_addr(addr, 0);
    if (r != I2C_OK) {
        return r;
    }
    i2c_clear_addr();
    if (!i2c_wait(I2C_SR1_TXE)) { return i2c_fail(); }
    I2C1_DR = reg;
    if (!i2c_wait(I2C_SR1_TXE)) { return i2c_fail(); }
    I2C1_DR = value;
    if (!i2c_wait(I2C_SR1_BTF)) { return i2c_fail(); }
    i2c_stop();
    i2c_stop_wait();
    return I2C_OK;
}

int i2c_read_regs(uint8_t addr, uint8_t reg, uint8_t *buf, uint8_t len)
{
    int r = i2c_select_reg(addr, reg);
    if (r != I2C_OK) {
        return r;
    }
    r = i2c_start_addr(addr, 1);                     /* repeated START, now read */
    if (r != I2C_OK) {
        return r;
    }
    I2C1_CR1 |= I2C_CR1_ACK;
    i2c_clear_addr();

    while (len > 3U) {
        if (!i2c_wait(I2C_SR1_RXNE)) { return i2c_fail(); }
        *buf++ = (uint8_t)I2C1_DR;
        len--;
    }
    /* the last three bytes need this exact order so that the final byte gets a NACK */
    if (!i2c_wait(I2C_SR1_BTF)) { return i2c_fail(); }
    I2C1_CR1 &= ~I2C_CR1_ACK;
    *buf++ = (uint8_t)I2C1_DR;                       /* byte N-2 */
    i2c_stop();
    *buf++ = (uint8_t)I2C1_DR;                       /* byte N-1 */
    if (!i2c_wait(I2C_SR1_RXNE)) { return i2c_fail(); }
    *buf = (uint8_t)I2C1_DR;                         /* byte N */
    i2c_stop_wait();
    I2C1_CR1 |= I2C_CR1_ACK;
    return I2C_OK;
}

int i2c_probe(uint8_t addr)
{
    if (i2c_start_addr(addr, 0) != I2C_OK) {
        return 0;
    }
    i2c_clear_addr();
    i2c_stop();
    i2c_stop_wait();
    return 1;
}
