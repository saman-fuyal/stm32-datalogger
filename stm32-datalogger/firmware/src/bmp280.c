/* BMP280 driver: factory constants, forced-mode measurement, Bosch compensation. */
#include "board.h"
#include "bmp280.h"
#include "config.h"
#include "i2c.h"

#define BMP_REG_CALIB     0x88U     /* 24 bytes of factory constants */
#define BMP_REG_ID        0xD0U
#define BMP_REG_RESET     0xE0U
#define BMP_REG_CTRL_MEAS 0xF4U
#define BMP_REG_CONFIG    0xF5U
#define BMP_REG_DATA      0xF7U     /* pressure then temperature, 6 bytes */

typedef struct {                    /* factory constants of this chip */
    uint16_t T1, P1;
    int16_t  T2, T3, P2, P3, P4, P5, P6, P7, P8, P9;
} bmp_cal_t;

static bmp_cal_t cal;
static int32_t   t_fine;
static uint8_t   bmp_ok = 0;
static uint32_t  bmp_retries = 0;   /* how often the first try failed */

static uint16_t u16le(const uint8_t *b) { return (uint16_t)(b[0] | (b[1] << 8)); }
static int16_t  s16le(const uint8_t *b) { return (int16_t)u16le(b); }

static int bmp_load_calibration(void)
{
    uint8_t c[24];
    if (i2c_read_regs(BMP_ADDR, BMP_REG_CALIB, c, 24) != I2C_OK) {
        return 0;
    }
    cal.T1 = u16le(&c[0]);   cal.T2 = s16le(&c[2]);   cal.T3 = s16le(&c[4]);
    cal.P1 = u16le(&c[6]);   cal.P2 = s16le(&c[8]);   cal.P3 = s16le(&c[10]);
    cal.P4 = s16le(&c[12]);  cal.P5 = s16le(&c[14]);  cal.P6 = s16le(&c[16]);
    cal.P7 = s16le(&c[18]);  cal.P8 = s16le(&c[20]);  cal.P9 = s16le(&c[22]);
    return 1;
}

/* result in hundredths of a degree C; also sets t_fine */
static int32_t bmp_comp_temp(int32_t adc_T)
{
    int32_t var1, var2;
    var1 = ((((adc_T >> 3) - ((int32_t)cal.T1 << 1))) * ((int32_t)cal.T2)) >> 11;
    var2 = (((((adc_T >> 4) - ((int32_t)cal.T1)) *
              ((adc_T >> 4) - ((int32_t)cal.T1))) >> 12) * ((int32_t)cal.T3)) >> 14;
    t_fine = var1 + var2;
    return (t_fine * 5 + 128) >> 8;
}

/* result in Pa (rounded); call bmp_comp_temp first */
static uint32_t bmp_comp_press(int32_t adc_P)
{
    int64_t var1, var2, p;

    var1 = (int64_t)t_fine - 128000;
    var2 = var1 * var1 * (int64_t)cal.P6;
    var2 = var2 + ((var1 * (int64_t)cal.P5) << 17);
    var2 = var2 + (((int64_t)cal.P4) << 35);
    var1 = ((var1 * var1 * (int64_t)cal.P3) >> 8) +
           ((var1 * (int64_t)cal.P2) << 12);
    var1 = ((((int64_t)1 << 47) + var1) * (int64_t)cal.P1) >> 33;
    if (var1 == 0) {
        return 0;
    }
    p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = ((int64_t)cal.P9 * (p >> 13) * (p >> 13)) >> 25;
    var2 = ((int64_t)cal.P8 * p) >> 19;
    p = ((p + var1 + var2) >> 8) + ((int64_t)cal.P7 << 4);

    /* p is Pa in Q24.8 (Pa * 256): convert to whole Pa with rounding */
    return (uint32_t)((p + 128) >> 8);
}

int bmp_init(void)
{
    uint8_t id;
    bmp_ok = 0;
    if (i2c_read_reg(BMP_ADDR, BMP_REG_ID, &id) != I2C_OK || (id != 0x58 && id != 0x60)) {
        return 0;
    }
    i2c_write_reg(BMP_ADDR, BMP_REG_RESET, 0xB6);        /* soft reset (config goes back to 0 = no filter) */
    wait_ms(10);
    if (!bmp_load_calibration()) {
        return 0;
    }
    if (cal.T1 == 0U || cal.T1 == 0xFFFFU) {             /* calibration read looks broken */
        return 0;
    }
    bmp_ok = 1;
    return 1;
}

int bmp_ready(void)
{
    return bmp_ok;
}

/* One measurement. Rejects impossible values (the BMP280 only works from
   -40 to +85 C, and nobody lives below 300 hPa or above 1100 hPa). */
int bmp_read_once(int32_t *temp_c100, uint32_t *press_pa)
{
    uint8_t d[6];
    int32_t adc_P, adc_T, t;
    uint32_t p;

    if (i2c_write_reg(BMP_ADDR, BMP_REG_CTRL_MEAS, BMP_CTRL_FORCED) != I2C_OK) {
        return BMP_ERR_START;
    }
    wait_ms(BMP_WAIT_MS);
    if (i2c_read_regs(BMP_ADDR, BMP_REG_DATA, d, 6) != I2C_OK) {
        return BMP_ERR_READ;
    }
    if (d[0] == 0x80 && d[1] == 0x00 && d[2] == 0x00) {  /* the 'no data yet' value */
        return BMP_ERR_NODATA;
    }
    adc_P = ((int32_t)d[0] << 12) | ((int32_t)d[1] << 4) | (d[2] >> 4);
    adc_T = ((int32_t)d[3] << 12) | ((int32_t)d[4] << 4) | (d[5] >> 4);
    t = bmp_comp_temp(adc_T);                            /* must come first */
    p = bmp_comp_press(adc_P);
    if (t < -4000 || t > 8500 || p < 30000U || p > 110000U) {
        return BMP_ERR_RANGE;
    }
    *temp_c100 = t;
    *press_pa = p;
    return BMP_OK;
}

int bmp_read(int32_t *temp_c100, uint32_t *press_pa)
{
    int r = bmp_read_once(temp_c100, press_pa);
    if (r == BMP_OK) {
        return BMP_OK;
    }
    bmp_retries++;
    wait_ms(20);
    r = bmp_read_once(temp_c100, press_pa);
    if (r == BMP_ERR_RANGE) {
        bmp_load_calibration();                          /* maybe the constants were damaged */
    }
    return r;
}

uint32_t bmp_retry_count(void)
{
    return bmp_retries;
}

const char *bmp_err_text(int code)
{
    switch (code) {
    case BMP_ERR_START: return "could not start a measurement";
    case BMP_ERR_READ:  return "I2C read failed";
    case BMP_ERR_RANGE: return "impossible value, rejected";
    default:            return "sensor returned no data";
    }
}

void bmp_selftest(int32_t *temp_c100, uint32_t *press_pa)
{
    static const bmp_cal_t example = {                   /* Bosch datasheet example */
        27504, 36477,                                    /* T1, P1 */
        26435, -1000,                                    /* T2, T3 */
        -10685, 3024, 2855, 140, -7, 15500, -14600, 6000 /* P2 .. P9 */
    };
    bmp_cal_t saved = cal;
    int32_t saved_t_fine = t_fine;

    cal = example;
    *temp_c100 = bmp_comp_temp(519888);
    *press_pa  = bmp_comp_press(415148);
    cal = saved;                                         /* this chip's constants back */
    t_fine = saved_t_fine;
}
