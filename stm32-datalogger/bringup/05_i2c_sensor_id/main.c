#include <stdint.h>

#define RCC_APB2ENR (*(volatile uint32_t *)0x40021018)
#define RCC_APB1ENR (*(volatile uint32_t *)0x4002101C)
#define GPIOA_CRH   (*(volatile uint32_t *)0x40010804)
#define GPIOB_CRL   (*(volatile uint32_t *)0x40010C00)
#define USART1_SR   (*(volatile uint32_t *)0x40013800)
#define USART1_DR   (*(volatile uint32_t *)0x40013804)
#define USART1_BRR  (*(volatile uint32_t *)0x40013808)
#define USART1_CR1  (*(volatile uint32_t *)0x4001380C)
#define I2C1_CR1    (*(volatile uint32_t *)0x40005400)
#define I2C1_CR2    (*(volatile uint32_t *)0x40005404)
#define I2C1_DR     (*(volatile uint32_t *)0x40005410)
#define I2C1_SR1    (*(volatile uint32_t *)0x40005414)
#define I2C1_SR2    (*(volatile uint32_t *)0x40005418)
#define I2C1_CCR    (*(volatile uint32_t *)0x4000541C)
#define I2C1_TRISE  (*(volatile uint32_t *)0x40005420)

#define I2C_WAIT_LOOPS  50000U
#define I2C_OK          0
#define I2C_ERR_NACK    1
#define I2C_ERR_TIMEOUT 2

/* ---------- UART (send only) ---------- */
static void uart_send_char(char c)
{
    while (!(USART1_SR & (1U << 7))) { }
    USART1_DR = c;
}

static void uart_send_string(const char *s)
{
    while (*s) {
        uart_send_char(*s++);
    }
}

static void uart_send_hex8(uint8_t v)
{
    const char *digits = "0123456789ABCDEF";
    uart_send_char(digits[v >> 4]);
    uart_send_char(digits[v & 0xFU]);
}

/* ---------- I2C ---------- */
static void i2c_init(void)
{
    I2C1_CR1 = (1U << 15);      /* software reset */
    I2C1_CR1 = 0;
    I2C1_CR2 = 8U;              /* peripheral clock = 8 MHz */
    I2C1_CCR = 40U;             /* 100 kHz: 8 MHz / (2 x 100 kHz) */
    I2C1_TRISE = 9U;
    I2C1_CR1 = 1U;              /* peripheral on */
}

static int i2c_fail(void)       /* bus got stuck: reset it */
{
    i2c_init();
    return I2C_ERR_TIMEOUT;
}

static int i2c_wait(uint32_t mask)   /* wait until a flag in SR1 is set */
{
    uint32_t t = I2C_WAIT_LOOPS;
    while (!(I2C1_SR1 & mask)) {
        if (--t == 0) {
            return 0;
        }
    }
    return 1;
}

static void i2c_stop(void)
{
    I2C1_CR1 |= (1U << 9);
}

static void i2c_clear_addr(void)     /* reading SR1 then SR2 clears ADDR */
{
    (void)I2C1_SR1;
    (void)I2C1_SR2;
}

/* START + address. rw: 0 = write, 1 = read. */
static int i2c_start_addr(uint8_t addr, uint8_t rw)
{
    uint32_t t = I2C_WAIT_LOOPS;

    I2C1_CR1 |= (1U << 8);                           /* START */
    if (!i2c_wait(1U << 0)) {                        /* SB: start sent */
        return i2c_fail();
    }
    I2C1_DR = (uint32_t)((addr << 1) | rw);

    while (!(I2C1_SR1 & ((1U << 1) | (1U << 10)))) { /* ADDR or AF */
        if (--t == 0) {
            return i2c_fail();
        }
    }
    if (I2C1_SR1 & (1U << 10)) {                     /* AF: nobody answered */
        I2C1_SR1 &= ~(1U << 10);
        i2c_stop();
        return I2C_ERR_NACK;
    }
    return I2C_OK;                                   /* caller clears ADDR */
}

/* read one register from a device */
static int i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *value)
{
    int r = i2c_start_addr(addr, 0);                 /* talk to the device */
    if (r != I2C_OK) {
        return r;
    }
    i2c_clear_addr();

    if (!i2c_wait(1U << 7)) {                        /* TXE: ready for a byte */
        return i2c_fail();
    }
    I2C1_DR = reg;                                   /* which register we want */
    if (!i2c_wait(1U << 2)) {                        /* BTF: byte sent */
        return i2c_fail();
    }

    r = i2c_start_addr(addr, 1);                     /* repeated START, now read */
    if (r != I2C_OK) {
        return r;
    }

    I2C1_CR1 &= ~(1U << 10);                         /* ACK off: only one byte */
    i2c_clear_addr();
    i2c_stop();                                      /* STOP right after ADDR */

    if (!i2c_wait(1U << 6)) {                        /* RXNE: byte arrived */
        return i2c_fail();
    }
    *value = (uint8_t)I2C1_DR;
    I2C1_CR1 |= (1U << 10);                          /* ACK back on */
    return I2C_OK;
}

/* ---------- ask a sensor for its ID ---------- */
static int check_sensor(uint8_t addr)
{
    uint8_t id;
    if (i2c_read_reg(addr, 0xD0, &id) != I2C_OK) {
        return 0;
    }
    uart_send_string("Sensor found at 0x");
    uart_send_hex8(addr);
    uart_send_string(", chip ID = 0x");
    uart_send_hex8(id);
    if (id == 0x60) {
        uart_send_string(" -> BME280 (temperature, pressure, humidity)\r\n");
    } else if (id == 0x58) {
        uart_send_string(" -> BMP280 (temperature, pressure, no humidity)\r\n");
    } else {
        uart_send_string(" -> unknown chip\r\n");
    }
    return 1;
}

int main(void)
{
    RCC_APB2ENR |= (1U << 2) | (1U << 3) | (1U << 14);   /* GPIOA, GPIOB, USART1 */
    RCC_APB1ENR |= (1U << 21);                           /* I2C1 */

    GPIOA_CRH &= ~(0xFU << 4);
    GPIOA_CRH |=  (0xBU << 4);                           /* PA9 = UART transmit */

    GPIOB_CRL &= ~((0xFU << 24) | (0xFU << 28));
    GPIOB_CRL |=  (0xFU << 24) | (0xFU << 28);           /* PB6, PB7 = I2C pins */

    USART1_BRR = 0x341;                                  /* 9600 baud */
    USART1_CR1 = (1U << 13) | (1U << 3);                 /* UART on, transmitter on */

    i2c_init();
    uart_send_string("\r\nsensor_i2c: looking for the sensor...\r\n");

    while (1)
    {
        if (!check_sensor(0x76) && !check_sensor(0x77)) {
            uart_send_string("No sensor found. Check VIN=3.3, GND, SCL=B6, SDA=B7\r\n");
        }
        for (volatile uint32_t i = 0; i < 1000000U; i++) { }   /* about 1 s */
    }
}
