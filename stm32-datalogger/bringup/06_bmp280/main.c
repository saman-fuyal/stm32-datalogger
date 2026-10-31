#include <stdint.h>

#define RCC_APB2ENR (*(volatile uint32_t *)0x40021018)
#define RCC_APB1ENR (*(volatile uint32_t *)0x4002101C)
#define GPIOA_CRH   (*(volatile uint32_t *)0x40010804)
#define GPIOB_CRL   (*(volatile uint32_t *)0x40010C00)
#define GPIOC_CRH   (*(volatile uint32_t *)0x40011004)
#define GPIOC_ODR   (*(volatile uint32_t *)0x4001100C)
#define USART1_SR   (*(volatile uint32_t *)0x40013800)
#define USART1_DR   (*(volatile uint32_t *)0x40013804)
#define USART1_BRR  (*(volatile uint32_t *)0x40013808)
#define USART1_CR1  (*(volatile uint32_t *)0x4001380C)
#define NVIC_ISER1  (*(volatile uint32_t *)0xE000E104)
#define STK_CTRL    (*(volatile uint32_t *)0xE000E010)
#define STK_LOAD    (*(volatile uint32_t *)0xE000E014)
#define STK_VAL     (*(volatile uint32_t *)0xE000E018)
#define I2C1_CR1    (*(volatile uint32_t *)0x40005400)
#define I2C1_CR2    (*(volatile uint32_t *)0x40005404)
#define I2C1_DR     (*(volatile uint32_t *)0x40005410)
#define I2C1_SR1    (*(volatile uint32_t *)0x40005414)
#define I2C1_SR2    (*(volatile uint32_t *)0x40005418)
#define I2C1_CCR    (*(volatile uint32_t *)0x4000541C)
#define I2C1_TRISE  (*(volatile uint32_t *)0x40005420)

#define BMP_ADDR        0x76U
#define I2C_WAIT_LOOPS  50000U
#define I2C_OK          0
#define I2C_ERR_NACK    1
#define I2C_ERR_TIMEOUT 2

/* ================= receive ring buffer ================= */
#define RX_SIZE 64U
static volatile uint8_t rx_head = 0;
static volatile uint8_t rx_tail = 0;
static volatile uint8_t rx_buf[RX_SIZE];

void USART1_IRQHandler(void)
{
    if (USART1_SR & (1U << 5)) {
        uint8_t c = (uint8_t)USART1_DR;
        uint8_t next = (rx_head + 1U) & (RX_SIZE - 1U);
        if (next != rx_tail) {
            rx_buf[rx_head] = c;
            rx_head = next;
        }
    }
}

static int rx_pop(uint8_t *out)
{
    if (rx_tail == rx_head) {
        return 0;
    }
    *out = rx_buf[rx_tail];
    rx_tail = (rx_tail + 1U) & (RX_SIZE - 1U);
    return 1;
}

/* ================= millisecond clock ================= */
static volatile uint32_t ms_ticks = 0;

void SysTick_Handler(void)
{
    ms_ticks++;
}

static void wait_ms(uint32_t ms)
{
    uint32_t start = ms_ticks;
    while ((ms_ticks - start) < ms) { }
}

/* ================= sending text ================= */
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

static void uart_send_number(uint32_t n)
{
    char tmp[10];
    uint8_t i = 0;
    if (n == 0) {
        uart_send_char('0');
        return;
    }
    while (n > 0) {
        tmp[i++] = (char)('0' + (n % 10U));
        n /= 10U;
    }
    while (i > 0) {
        uart_send_char(tmp[--i]);
    }
}

static void print_fixed2(int32_t v)          /* prints v/100, like 2534 -> 25.34 */
{
    uint32_t frac;
    if (v < 0) {
        uart_send_char('-');
        v = -v;
    }
    uart_send_number((uint32_t)(v / 100));
    uart_send_char('.');
    frac = (uint32_t)(v % 100);
    if (frac < 10U) {
        uart_send_char('0');
    }
    uart_send_number(frac);
}

/* ================= I2C ================= */
static void i2c_init(void)
{
    I2C1_CR1 = (1U << 15);      /* software reset */
    I2C1_CR1 = 0;
    I2C1_CR2 = 8U;              /* peripheral clock = 8 MHz */
    I2C1_CCR = 40U;             /* 100 kHz */
    I2C1_TRISE = 9U;
    I2C1_CR1 = 1U;              /* peripheral on */
}

static int i2c_fail(void)       /* bus got stuck: reset it */
{
    i2c_init();
    return I2C_ERR_TIMEOUT;
}

static int i2c_wait(uint32_t mask)
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

static void i2c_stop_wait(void)
{
    uint32_t t = I2C_WAIT_LOOPS;
    while ((I2C1_CR1 & (1U << 9)) && --t) { }
}

static void i2c_clear_addr(void)
{
    (void)I2C1_SR1;
    (void)I2C1_SR2;
}

/* START + address. rw: 0 = write, 1 = read. */
static int i2c_start_addr(uint8_t addr, uint8_t rw)
{
    uint32_t t = I2C_WAIT_LOOPS;

    I2C1_CR1 |= (1U << 8);                           /* START */
    if (!i2c_wait(1U << 0)) {
        return i2c_fail();
    }
    I2C1_DR = (uint32_t)((addr << 1) | rw);

    while (!(I2C1_SR1 & ((1U << 1) | (1U << 10)))) { /* ADDR or AF */
        if (--t == 0) {
            return i2c_fail();
        }
    }
    if (I2C1_SR1 & (1U << 10)) {                     /* nobody answered */
        I2C1_SR1 &= ~(1U << 10);
        i2c_stop();
        return I2C_ERR_NACK;
    }
    return I2C_OK;
}

static int i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *value)
{
    int r = i2c_start_addr(addr, 0);
    if (r != I2C_OK) {
        return r;
    }
    i2c_clear_addr();
    if (!i2c_wait(1U << 7)) { return i2c_fail(); }
    I2C1_DR = reg;
    if (!i2c_wait(1U << 2)) { return i2c_fail(); }

    r = i2c_start_addr(addr, 1);
    if (r != I2C_OK) {
        return r;
    }
    I2C1_CR1 &= ~(1U << 10);                         /* ACK off: one byte */
    i2c_clear_addr();
    i2c_stop();
    if (!i2c_wait(1U << 6)) { return i2c_fail(); }
    *value = (uint8_t)I2C1_DR;
    i2c_stop_wait();
    I2C1_CR1 |= (1U << 10);                          /* ACK on again */
    return I2C_OK;
}

static int i2c_write_reg(uint8_t addr, uint8_t reg, uint8_t value)
{
    int r = i2c_start_addr(addr, 0);
    if (r != I2C_OK) {
        return r;
    }
    i2c_clear_addr();
    if (!i2c_wait(1U << 7)) { return i2c_fail(); }
    I2C1_DR = reg;
    if (!i2c_wait(1U << 7)) { return i2c_fail(); }
    I2C1_DR = value;
    if (!i2c_wait(1U << 2)) { return i2c_fail(); }
    i2c_stop();
    i2c_stop_wait();
    return I2C_OK;
}

/* read len bytes (len must be 3 or more) starting at register reg */
static int i2c_read_regs(uint8_t addr, uint8_t reg, uint8_t *buf, uint8_t len)
{
    int r = i2c_start_addr(addr, 0);
    if (r != I2C_OK) {
        return r;
    }
    i2c_clear_addr();
    if (!i2c_wait(1U << 7)) { return i2c_fail(); }
    I2C1_DR = reg;
    if (!i2c_wait(1U << 2)) { return i2c_fail(); }

    r = i2c_start_addr(addr, 1);
    if (r != I2C_OK) {
        return r;
    }
    I2C1_CR1 |= (1U << 10);                          /* ACK on */
    i2c_clear_addr();

    while (len > 3U) {
        if (!i2c_wait(1U << 6)) { return i2c_fail(); }
        *buf++ = (uint8_t)I2C1_DR;
        len--;
    }
    /* last three bytes need a special order so the final byte is NACKed */
    if (!i2c_wait(1U << 2)) { return i2c_fail(); }
    I2C1_CR1 &= ~(1U << 10);                         /* ACK off */
    *buf++ = (uint8_t)I2C1_DR;                       /* byte N-2 */
    i2c_stop();
    *buf++ = (uint8_t)I2C1_DR;                       /* byte N-1 */
    if (!i2c_wait(1U << 6)) { return i2c_fail(); }
    *buf = (uint8_t)I2C1_DR;                         /* byte N */
    i2c_stop_wait();
    I2C1_CR1 |= (1U << 10);
    return I2C_OK;
}

/* ================= BMP280 driver ================= */
static uint16_t dig_T1, dig_P1;
static int16_t  dig_T2, dig_T3;
static int16_t  dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
static int32_t  t_fine;

static uint16_t u16le(const uint8_t *b) { return (uint16_t)(b[0] | (b[1] << 8)); }
static int16_t  s16le(const uint8_t *b) { return (int16_t)u16le(b); }

static int bmp_load_calibration(void)
{
    uint8_t c[24];
    if (i2c_read_regs(BMP_ADDR, 0x88, c, 24) != I2C_OK) {
        return I2C_ERR_TIMEOUT;
    }
    dig_T1 = u16le(&c[0]);   dig_T2 = s16le(&c[2]);   dig_T3 = s16le(&c[4]);
    dig_P1 = u16le(&c[6]);   dig_P2 = s16le(&c[8]);   dig_P3 = s16le(&c[10]);
    dig_P4 = s16le(&c[12]);  dig_P5 = s16le(&c[14]);  dig_P6 = s16le(&c[16]);
    dig_P7 = s16le(&c[18]);  dig_P8 = s16le(&c[20]);  dig_P9 = s16le(&c[22]);
    return I2C_OK;
}

/* result in hundredths of a degree C; also sets t_fine */
static int32_t bmp_comp_temp(int32_t adc_T)
{
    int32_t var1, var2;
    var1 = ((((adc_T >> 3) - ((int32_t)dig_T1 << 1))) * ((int32_t)dig_T2)) >> 11;
    var2 = (((((adc_T >> 4) - ((int32_t)dig_T1)) *
              ((adc_T >> 4) - ((int32_t)dig_T1))) >> 12) * ((int32_t)dig_T3)) >> 14;
    t_fine = var1 + var2;
    return (t_fine * 5 + 128) >> 8;
}

/* result in Pa; call bmp_comp_temp first */
/* result in Pa (rounded); call bmp_comp_temp first */
static uint32_t bmp_comp_press(int32_t adc_P)
{
    int64_t var1, var2, p;

    var1 = (int64_t)t_fine - 128000;
    var2 = var1 * var1 * (int64_t)dig_P6;
    var2 = var2 + ((var1 * (int64_t)dig_P5) << 17);
    var2 = var2 + (((int64_t)dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)dig_P3) >> 8) +
           ((var1 * (int64_t)dig_P2) << 12);
    var1 = ((((int64_t)1 << 47) + var1) * (int64_t)dig_P1) >> 33;
    if (var1 == 0) {
        return 0;
    }
    p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = ((int64_t)dig_P9 * (p >> 13) * (p >> 13)) >> 25;
    var2 = ((int64_t)dig_P8 * p) >> 19;
    p = ((p + var1 + var2) >> 8) + ((int64_t)dig_P7 << 4);

    /* p is Pa in Q24.8 (Pa * 256); convert to whole Pa with rounding */
    return (uint32_t)((p + 128) >> 8);
}

static uint8_t sensor_ok = 0;

static int sensor_init(void)
{
    uint8_t id, v;
    if (i2c_read_reg(BMP_ADDR, 0xD0, &id) != I2C_OK || (id != 0x58 && id != 0x60)) {
        return 0;
    }
    i2c_write_reg(BMP_ADDR, 0xE0, 0xB6);                  /* soft reset */
    for (volatile uint32_t i = 0; i < 100000U; i++) { }   /* let the reset finish */
    if (bmp_load_calibration() != I2C_OK) {
        return 0;
    }
    i2c_write_reg(BMP_ADDR, 0xF5, 0x00);                  /* config: no filter */
    if (i2c_write_reg(BMP_ADDR, 0xF4, 0x27) != I2C_OK) { /* temp x1, pressure x1, normal mode */
        return 0;
    }
    if (i2c_read_reg(BMP_ADDR, 0xF4, &v) != I2C_OK || v != 0x27) {
        return 0;                                          /* the write did not stick */
    }
    sensor_ok = 1;
    return 1;
}

static int ensure_sensor(void)
{
    return sensor_ok || sensor_init();
}

static void measure_and_print(void)
{
    uint8_t d[6];
    int32_t adc_P, adc_T, t;
    uint32_t p;

    i2c_write_reg(BMP_ADDR, 0xF4, 0x25);   /* temp x1, pressure x1, forced: measure once */
    wait_ms(15);                           /* a measurement takes under 10 ms */

    if (i2c_read_regs(BMP_ADDR, 0xF7, d, 6) != I2C_OK) {
        uart_send_string("Read error\r\n");
        return;
    }
    if (d[0] == 0x80 && d[1] == 0x00 && d[2] == 0x00) {
        uart_send_string("No measurement yet (raw = 80 00 00), try debug\r\n");
        return;
    }
    adc_P = ((int32_t)d[0] << 12) | ((int32_t)d[1] << 4) | (d[2] >> 4);
    adc_T = ((int32_t)d[3] << 12) | ((int32_t)d[4] << 4) | (d[5] >> 4);
    t = bmp_comp_temp(adc_T);              /* must come first */
    p = bmp_comp_press(adc_P);

    uart_send_string("Temp = ");
    print_fixed2(t);
    uart_send_string(" C, Pressure = ");
    print_fixed2((int32_t)p);
    uart_send_string(" hPa\r\n");
}

/* ================= text helpers ================= */
static int str_equal(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static int starts_with(const char *s, const char *prefix)
{
    while (*prefix) {
        if (*s++ != *prefix++) {
            return 0;
        }
    }
    return 1;
}

static int parse_number(const char *s, uint32_t *out)
{
    uint32_t v = 0;
    if (*s == '\0') {
        return 0;
    }
    while (*s) {
        if (*s < '0' || *s > '9') {
            return 0;
        }
        v = v * 10U + (uint32_t)(*s - '0');
        if (v > 100000U) {
            return 0;
        }
        s++;
    }
    *out = v;
    return 1;
}

/* ================= command line ================= */
#define LINE_SIZE 32U
static char    line[LINE_SIZE];
static uint8_t line_len = 0;

static uint8_t  running = 0;
static uint32_t rate_ms = 1000;
static uint32_t last_sample = 0;

static void handle_line(const char *cmd)
{
    uint32_t n;

    if (cmd[0] == '\0') {
        return;
    }

    if (str_equal(cmd, "start")) {
        if (!ensure_sensor()) {
            uart_send_string("ERR sensor not ready, check wiring\r\n");
        } else {
            running = 1;
            GPIOC_ODR &= ~(1U << 13);               /* LED on while logging */
            uart_send_string("OK started\r\n");
            measure_and_print();
            last_sample = ms_ticks;
        }
    } else if (str_equal(cmd, "stop")) {
        running = 0;
        GPIOC_ODR |= (1U << 13);                    /* LED off */
        uart_send_string("OK stopped\r\n");
    } else if (starts_with(cmd, "set rate ")) {
        if (parse_number(cmd + 9, &n) && n >= 100U && n <= 60000U) {
            rate_ms = n;
            uart_send_string("OK rate = ");
            uart_send_number(rate_ms);
            uart_send_string(" ms\r\n");
        } else {
            uart_send_string("ERR rate must be a number from 100 to 60000\r\n");
        }
    } else if (str_equal(cmd, "status")) {
        uart_send_string(running ? "running" : "stopped");
        uart_send_string(", rate = ");
        uart_send_number(rate_ms);
        uart_send_string(" ms, sensor ");
        uart_send_string(sensor_ok ? "ok\r\n" : "not ready\r\n");
    } else if (str_equal(cmd, "read")) {
        if (ensure_sensor()) {
            measure_and_print();
        } else {
            uart_send_string("ERR sensor not ready, check wiring\r\n");
        }
    } else if (str_equal(cmd, "debug")) {
        uint8_t v, d[6], i;
        if (i2c_read_reg(BMP_ADDR, 0xD0, &v) == I2C_OK) {
            uart_send_string("chip ID = 0x");
            uart_send_hex8(v);
            uart_send_string("\r\n");
        }
        if (i2c_read_reg(BMP_ADDR, 0xF4, &v) == I2C_OK) {
            uart_send_string("ctrl_meas before = 0x");
            uart_send_hex8(v);
            uart_send_string("\r\n");
        }
        i2c_write_reg(BMP_ADDR, 0xF4, 0x25);        /* ask for one measurement */
        wait_ms(15);
        if (i2c_read_reg(BMP_ADDR, 0xF3, &v) == I2C_OK) {
            uart_send_string("status (0xF3) = 0x");
            uart_send_hex8(v);
            uart_send_string("\r\n");
        }
        if (i2c_read_regs(BMP_ADDR, 0xF7, d, 6) == I2C_OK) {
            uart_send_string("raw bytes F7..FC = ");
            for (i = 0; i < 6; i++) {
                uart_send_hex8(d[i]);
                uart_send_char(' ');
            }
            uart_send_string("\r\n");
        }
        if (i2c_read_reg(BMP_ADDR, 0xF4, &v) == I2C_OK) {
            uart_send_string("ctrl_meas after = 0x");
            uart_send_hex8(v);
            uart_send_string("\r\n");
        }
        uart_send_string("dig_T1 = ");
        uart_send_number(dig_T1);
        uart_send_string(", dig_P1 = ");
        uart_send_number(dig_P1);
        uart_send_string("\r\n");
    } else if (str_equal(cmd, "selftest")) {
        int32_t  t;
        uint32_t p;
        dig_T1 = 27504;  dig_T2 = 26435;  dig_T3 = -1000;
        dig_P1 = 36477;  dig_P2 = -10685; dig_P3 = 3024;
        dig_P4 = 2855;   dig_P5 = 140;    dig_P6 = -7;
        dig_P7 = 15500;  dig_P8 = -14600; dig_P9 = 6000;
        t = bmp_comp_temp(519888);
        p = bmp_comp_press(415148);
        uart_send_string("Temp = ");
        print_fixed2(t);
        uart_send_string(" C (expect 25.08), Pressure = ");
        print_fixed2((int32_t)p);
        uart_send_string(" hPa (expect 1006.53)\r\n");
        bmp_load_calibration();              /* put the sensor's real values back */ } else {
        uart_send_string("ERR unknown command\r\n");
    }
}

int main(void)
{
    RCC_APB2ENR |= (1U << 2) | (1U << 3) | (1U << 4) | (1U << 14);  /* GPIOA, B, C, USART1 */
    RCC_APB1ENR |= (1U << 21);                                       /* I2C1 */

    GPIOA_CRH &= ~((0xFU << 4) | (0xFU << 8));
    GPIOA_CRH |=  (0xBU << 4) | (0x4U << 8);             /* PA9 = TX, PA10 = RX */

    GPIOB_CRL &= ~((0xFU << 24) | (0xFU << 28));
    GPIOB_CRL |=  (0xFU << 24) | (0xFU << 28);           /* PB6, PB7 = I2C pins */

    GPIOC_CRH &= ~(0xFU << 20);
    GPIOC_CRH |=  (0x2U << 20);                          /* PC13 = LED output */
    GPIOC_ODR |=  (1U << 13);                            /* LED off */

    USART1_BRR = 0x341;                                  /* 9600 baud */
    USART1_CR1 = (1U << 13) | (1U << 5) | (1U << 3) | (1U << 2);
    NVIC_ISER1 = (1U << 5);

    STK_LOAD = 8000U - 1U;                               /* 1 ms tick at 8 MHz */
    STK_VAL  = 0;
    STK_CTRL = 7U;

    i2c_init();

    uart_send_string("\r\nBMP280 logger. Commands: start, stop, set rate N, status, read, debug\r\n");
    if (sensor_init()) {
        uart_send_string("Sensor ready\r\n");
    } else {
        uart_send_string("Sensor NOT ready (check wiring, or see debug)\r\n");
    }
    uart_send_string("> ");

    while (1)
    {
        uint8_t c;

        if (running && (ms_ticks - last_sample) >= rate_ms) {
            last_sample = ms_ticks;
            measure_and_print();
        }

        if (rx_pop(&c)) {
            if (c == '\r' || c == '\n') {
                uart_send_string("\r\n");
                line[line_len] = '\0';
                handle_line(line);
                line_len = 0;
                uart_send_string("> ");
            } else if (c == 0x08 || c == 0x7F) {
                if (line_len > 0) {
                    line_len--;
                    uart_send_string("\b \b");
                }
            } else if (line_len < LINE_SIZE - 1U) {
                line[line_len++] = (char)c;
                uart_send_char((char)c);
            }
        }
    }
}
