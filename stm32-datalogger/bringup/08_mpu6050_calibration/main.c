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

#define MPU_ADDR        0x68U
#define I2C_WAIT_LOOPS  50000U
#define I2C_OK          0
#define I2C_ERR_NACK    1
#define I2C_ERR_TIMEOUT 2

/* ---- accelerometer calibration values: paste the lines printed by 'cal done' here ---- */

#define ACC_OFF_X 653
#define ACC_OFF_Y 46
#define ACC_OFF_Z 925
#define ACC_SCALE_X 16375
#define ACC_SCALE_Y 16356
#define ACC_SCALE_Z 16510



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

static void uart_send_int(int32_t v)
{
    if (v < 0) {
        uart_send_char('-');
        v = -v;
    }
    uart_send_number((uint32_t)v);
}

static void print_fixed2(int32_t v)          /* prints v/100, like -253 -> -2.53 */
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

static void print_fixed3(int32_t v)          /* prints v/1000, like -998 -> -0.998 */
{
    uint32_t frac;
    if (v < 0) {
        uart_send_char('-');
        v = -v;
    }
    uart_send_number((uint32_t)(v / 1000));
    uart_send_char('.');
    frac = (uint32_t)(v % 1000);
    if (frac < 100U) {
        uart_send_char('0');
    }
    if (frac < 10U) {
        uart_send_char('0');
    }
    uart_send_number(frac);
}

static uint32_t isqrt32(uint32_t n)          /* integer square root */
{
    uint32_t r = 0;
    uint32_t bit = 1UL << 30;
    while (bit > n) {
        bit >>= 2;
    }
    while (bit) {
        if (n >= r + bit) {
            n -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
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
        i2c_stop_wait();
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

/* returns 1 if a device answers at this 7-bit address */
static int i2c_probe(uint8_t addr)
{
    if (i2c_start_addr(addr, 0) != I2C_OK) {
        return 0;
    }
    i2c_clear_addr();
    i2c_stop();
    i2c_stop_wait();
    return 1;
}

/* ================= MPU6050 driver ================= */
static int16_t gyro_off[3] = { 0, 0, 0 };                 /* raw counts, measured by gyro_zero */
static int32_t acc_off[3]   = { ACC_OFF_X, ACC_OFF_Y, ACC_OFF_Z };
static int32_t acc_scale[3] = { ACC_SCALE_X, ACC_SCALE_Y, ACC_SCALE_Z };
static int32_t cal_avg[6][3];                             /* average raw acc per calibration face */
static uint8_t cal_have = 0;                              /* bit mask of faces captured */
static uint8_t sensor_ok = 0;

static int mpu_init(void)
{
    uint8_t id, v;

    if (i2c_read_reg(MPU_ADDR, 0x75, &id) != I2C_OK) {     /* WHO_AM_I */
        return 0;
    }
    uart_send_string("WHO_AM_I = 0x");
    uart_send_hex8(id);
    if (id == 0x68) {
        uart_send_string(" (MPU6050)\r\n");
    } else if (id == 0x70) {
        uart_send_string(" (MPU6500 type, not a real MPU6050 but compatible)\r\n");
    } else {
        uart_send_string(" (unknown, trying anyway)\r\n");
    }

    i2c_write_reg(MPU_ADDR, 0x6B, 0x80);                   /* reset the chip */
    wait_ms(100);
    if (i2c_write_reg(MPU_ADDR, 0x6B, 0x01) != I2C_OK) {   /* wake up, clock from gyro */
        return 0;
    }
    wait_ms(10);
    i2c_write_reg(MPU_ADDR, 0x1A, 0x03);                   /* low-pass filter about 44 Hz */
    i2c_write_reg(MPU_ADDR, 0x19, 9);                      /* 1 kHz / (1 + 9) = 100 samples/s */
    i2c_write_reg(MPU_ADDR, 0x1B, 0x00);                   /* gyro range +-250 deg/s */
    i2c_write_reg(MPU_ADDR, 0x1C, 0x00);                   /* accel range +-2 g */

    if (i2c_read_reg(MPU_ADDR, 0x6B, &v) != I2C_OK || (v & 0x40U)) {
        return 0;                                          /* still asleep */
    }
    sensor_ok = 1;
    return 1;
}

static int ensure_sensor(void)
{
    return sensor_ok || mpu_init();
}

/* v[0..2] = acceleration X,Y,Z   v[3] = temperature   v[4..6] = gyro X,Y,Z */
static int mpu_read_raw(int16_t v[7])
{
    uint8_t d[14];
    uint8_t i;
    if (i2c_read_regs(MPU_ADDR, 0x3B, d, 14) != I2C_OK) {
        return 0;
    }
    for (i = 0; i < 7; i++) {                              /* high byte comes first */
        v[i] = (int16_t)((d[2 * i] << 8) | d[2 * i + 1]);
    }
    return 1;
}

/* ---------- gyro: measure the zero offset while the sensor is still ---------- */
static int gyro_zero(void)
{
    int16_t v[7];
    int32_t sum[3] = { 0, 0, 0 };
    uint8_t i, k, good = 0;

    for (i = 0; i < 100U; i++) {
        if (mpu_read_raw(v)) {
            sum[0] += v[4];
            sum[1] += v[5];
            sum[2] += v[6];
            good++;
        }
        wait_ms(10);
    }
    if (good < 50U) {
        return 0;
    }
    for (k = 0; k < 3; k++) {
        gyro_off[k] = (int16_t)(sum[k] / good);
    }
    return 1;
}

/* ---------- gyro: add up the rotation over 5 seconds (rough scale check) ---------- */
static void gyro_spin_test(void)
{
    int16_t v[7];
    int32_t sum[3] = { 0, 0, 0 };
    uint32_t last, now, end;
    uint8_t k;

    uart_send_string("Rotate the sensor slowly now, then hold still (5 seconds)...\r\n");
    last = ms_ticks;
    end  = last + 5000U;
    while ((int32_t)(end - ms_ticks) > 0) {
        if (mpu_read_raw(v)) {
            now = ms_ticks;
            for (k = 0; k < 3; k++) {
                sum[k] += ((int32_t)v[4 + k] - gyro_off[k]) * (int32_t)(now - last);
            }
            last = now;
        }
        wait_ms(5);
    }
    uart_send_string("Total rotation in degrees (X, Y, Z): ");
    for (k = 0; k < 3; k++) {
        print_fixed2(sum[k] / 1310);                       /* counts*ms / 131 / 1000 = deg */
        uart_send_char(' ');
    }
    uart_send_string("\r\n");
}

/* ---------- accelerometer six-position calibration ---------- */
static void print_cal_help(void)
{
    uart_send_string("Accelerometer calibration, 6 faces (use the X/Y arrows printed on the board):\r\n");
    uart_send_string(" cal 1 = Z up   (flat, chip side up)\r\n");
    uart_send_string(" cal 2 = Z down (flat, chip side down)\r\n");
    uart_send_string(" cal 3 = X up      cal 4 = X down\r\n");
    uart_send_string(" cal 5 = Y up      cal 6 = Y down\r\n");
    uart_send_string(" cal done = compute and apply,  cal clear = start over\r\n");
    uart_send_string("After cal N you have 5 seconds to place it, then hold still.\r\n");
}

static void acc_capture(uint8_t n)                         /* n = 1..6 */
{
    int16_t v[7];
    int32_t sum[3] = { 0, 0, 0 };
    int32_t avg[3];
    uint8_t i, k, good = 0;
    uint8_t idx  = (uint8_t)(n - 1U);
    uint8_t axis = (idx < 2U) ? 2U : ((idx < 4U) ? 0U : 1U);
    int32_t main_v;

    uart_send_string("Get it in position... ");
    for (i = 5; i > 0; i--) {
        uart_send_number(i);
        uart_send_char(' ');
        wait_ms(1000);
    }
    uart_send_string("hold still!\r\n");

    for (i = 0; i < 200U; i++) {
        if (mpu_read_raw(v)) {
            for (k = 0; k < 3; k++) {
                sum[k] += v[k];
            }
            good++;
        }
        wait_ms(10);
    }
    if (good < 100U) {
        uart_send_string("ERR too many read errors\r\n");
        return;
    }
    for (k = 0; k < 3; k++) {
        avg[k] = sum[k] / good;
    }

    main_v = (idx % 2U == 0U) ? avg[axis] : -avg[axis];
    if (main_v < 12000) {
        uart_send_string("ERR wrong position: that axis does not read about 1 g. Try again.\r\n");
        return;
    }
    for (k = 0; k < 3; k++) {
        cal_avg[idx][k] = avg[k];
    }
    cal_have |= (uint8_t)(1U << idx);

    uart_send_string("Saved face ");
    uart_send_number(n);
    uart_send_string(": X=");
    uart_send_int(avg[0]);
    uart_send_string(" Y=");
    uart_send_int(avg[1]);
    uart_send_string(" Z=");
    uart_send_int(avg[2]);
    uart_send_string(" (raw counts)\r\n");

    for (k = 0; k < 3; k++) {
        if (k != axis && (avg[k] > 2000 || avg[k] < -2000)) {
            uart_send_string("WARNING: tilted a bit, the other two axes should be near 0. Redo this face for best accuracy.\r\n");
            break;
        }
    }
    uart_send_string("Faces done: ");
    for (k = 0; k < 6; k++) {
        uart_send_char((cal_have & (1U << k)) ? (char)('1' + k) : '-');
    }
    uart_send_string("\r\n");
}

static void acc_cal_finish(void)
{
    static const uint8_t plus_idx[3]  = { 2, 4, 0 };       /* face where X, Y, Z points up */
    static const uint8_t minus_idx[3] = { 3, 5, 1 };       /* face where X, Y, Z points down */
    const char names[3] = { 'X', 'Y', 'Z' };
    int32_t off[3], scl[3];
    uint8_t a;

    if (cal_have != 0x3FU) {
        uart_send_string("ERR not all six faces captured yet (cal 1 ... cal 6)\r\n");
        return;
    }
    for (a = 0; a < 3; a++) {
        int32_t plus  = cal_avg[plus_idx[a]][a];
        int32_t minus = cal_avg[minus_idx[a]][a];
        off[a] = (plus + minus) / 2;                       /* offset = middle of the two readings */
        scl[a] = (plus - minus) / 2;                       /* counts per g */
        if (scl[a] < 13900 || scl[a] > 18800) {
            uart_send_string("ERR axis ");
            uart_send_char(names[a]);
            uart_send_string(" scale looks wrong, redo its two faces\r\n");
            return;
        }
    }
    for (a = 0; a < 3; a++) {
        acc_off[a]   = off[a];
        acc_scale[a] = scl[a];
    }

    for (a = 0; a < 3; a++) {
        uart_send_char(names[a]);
        uart_send_string(": offset ");
        print_fixed3(off[a] * 1000 / 16384);
        uart_send_string(" g, scale ");
        print_fixed2(scl[a] * 10000 / 16384);
        uart_send_string(" % of nominal\r\n");
    }
    uart_send_string("Applied now. To keep it after a reset, paste these at the top of main.c:\r\n");
    for (a = 0; a < 3; a++) {
        uart_send_string("#define ACC_OFF_");
        uart_send_char(names[a]);
        uart_send_char(' ');
        uart_send_int(off[a]);
        uart_send_string("\r\n");
    }
    for (a = 0; a < 3; a++) {
        uart_send_string("#define ACC_SCALE_");
        uart_send_char(names[a]);
        uart_send_char(' ');
        uart_send_int(scl[a]);
        uart_send_string("\r\n");
    }
}

static void print_sample(void)
{
    int16_t v[7];
    int32_t g[3];
    uint32_t m2;
    uint8_t i;

    if (!mpu_read_raw(v)) {
        uart_send_string("Read error\r\n");
        return;
    }
    for (i = 0; i < 3; i++) {
        g[i] = ((int32_t)v[i] - acc_off[i]) * 1000 / acc_scale[i];   /* thousandths of a g */
    }
    m2 = (uint32_t)(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);

    uart_send_string("Acc g: ");
    for (i = 0; i < 3; i++) {
        print_fixed3(g[i]);
        uart_send_char(' ');
    }
    uart_send_string(" |a| = ");
    print_fixed3((int32_t)isqrt32(m2));                    /* should be 1.000 at rest */
    uart_send_string("  Gyro dps: ");
    for (i = 0; i < 3; i++) {
        print_fixed2(((int32_t)v[4 + i] - gyro_off[i]) * 100 / 131);   /* 131 counts = 1 deg/s */
        uart_send_char(' ');
    }
    uart_send_string("\r\n");
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
            GPIOC_ODR &= ~(1U << 13);               /* LED on while streaming */
            uart_send_string("OK started\r\n");
            print_sample();
            last_sample = ms_ticks;
        }
    } else if (str_equal(cmd, "stop")) {
        running = 0;
        GPIOC_ODR |= (1U << 13);                    /* LED off */
        uart_send_string("OK stopped\r\n");
    } else if (starts_with(cmd, "set rate ")) {
        if (parse_number(cmd + 9, &n) && n >= 200U && n <= 60000U) {
            rate_ms = n;
            uart_send_string("OK rate = ");
            uart_send_number(rate_ms);
            uart_send_string(" ms\r\n");
        } else {
            uart_send_string("ERR rate must be a number from 200 to 60000\r\n");
        }
    } else if (str_equal(cmd, "status")) {
        uart_send_string(running ? "running" : "stopped");
        uart_send_string(", rate = ");
        uart_send_number(rate_ms);
        uart_send_string(" ms, sensor ");
        uart_send_string(sensor_ok ? "ok\r\n" : "not ready\r\n");
    } else if (str_equal(cmd, "read")) {
        if (ensure_sensor()) {
            print_sample();
        } else {
            uart_send_string("ERR sensor not ready, check wiring\r\n");
        }
    } else if (str_equal(cmd, "zero")) {
        if (!ensure_sensor()) {
            uart_send_string("ERR sensor not ready, check wiring\r\n");
        } else {
            uart_send_string("Keep the sensor still for 1 second...\r\n");
            uart_send_string(gyro_zero() ? "OK gyro zeroed\r\n" : "ERR too many read errors\r\n");
        }
    } else if (str_equal(cmd, "spin")) {
        if (ensure_sensor()) {
            gyro_spin_test();
        } else {
            uart_send_string("ERR sensor not ready, check wiring\r\n");
        }
    } else if (str_equal(cmd, "cal")) {
        print_cal_help();
    } else if (starts_with(cmd, "cal ")) {
        const char *arg = cmd + 4;
        if (!ensure_sensor()) {
            uart_send_string("ERR sensor not ready, check wiring\r\n");
        } else if (arg[0] >= '1' && arg[0] <= '6' && arg[1] == '\0') {
            acc_capture((uint8_t)(arg[0] - '0'));
        } else if (str_equal(arg, "done")) {
            acc_cal_finish();
        } else if (str_equal(arg, "clear")) {
            uint8_t a;
            cal_have = 0;
            for (a = 0; a < 3; a++) {
                acc_off[a] = 0;
                acc_scale[a] = 16384;
            }
            uart_send_string("OK calibration cleared (raw nominal values in use)\r\n");
        } else {
            print_cal_help();
        }
    } else if (str_equal(cmd, "scan")) {
        uint8_t a, count = 0;
        uart_send_string("Scanning...\r\n");
        for (a = 0x08; a <= 0x77; a++) {
            if (i2c_probe(a)) {
                uart_send_string("Found 0x");
                uart_send_hex8(a);
                if (a == 0x68 || a == 0x69) {
                    uart_send_string(" (MPU6050 range)");
                }
                if (a == 0x76 || a == 0x77) {
                    uart_send_string(" (BMP280 range)");
                }
                uart_send_string("\r\n");
                count++;
            }
        }
        if (count == 0) {
            uart_send_string("No devices found\r\n");
        }
    } else {
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

    uart_send_string("\r\nMPU6050 reader. Commands: read, start, stop, set rate N, zero, cal, spin, status, scan\r\n");
    if (mpu_init()) {
        uart_send_string("Sensor ready. Zeroing gyro, keep it still for 1 second...\r\n");
        uart_send_string(gyro_zero() ? "Gyro zeroed\r\n" : "Gyro zero failed\r\n");
    } else {
        uart_send_string("Sensor NOT ready, check wiring (try scan)\r\n");
    }
    uart_send_string("> ");

    while (1)
    {
        uint8_t c;

        if (running && (ms_ticks - last_sample) >= rate_ms) {
            last_sample = ms_ticks;
            print_sample();
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
