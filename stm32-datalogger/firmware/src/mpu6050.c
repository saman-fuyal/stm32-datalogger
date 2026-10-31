/* MPU6050 driver. Calibration constants come from config.h (bringup/08 'cal done'). */
#include "board.h"
#include "config.h"
#include "i2c.h"
#include "mpu6050.h"

#define MPU_REG_SMPLRT_DIV   0x19U
#define MPU_REG_CONFIG       0x1AU
#define MPU_REG_GYRO_CONFIG  0x1BU
#define MPU_REG_ACCEL_CONFIG 0x1CU
#define MPU_REG_ACCEL_XOUT_H 0x3BU  /* acceleration, temperature, gyro: 14 bytes */
#define MPU_REG_PWR_MGMT_1   0x6BU
#define MPU_REG_WHO_AM_I     0x75U
#define MPU_SLEEP_BIT        0x40U

static int16_t gyro_off[3] = { 0, 0, 0 };            /* raw counts, measured by gyro_zero */
static const int32_t acc_off[3]   = { ACC_OFF_X, ACC_OFF_Y, ACC_OFF_Z };
static const int32_t acc_scale[3] = { ACC_SCALE_X, ACC_SCALE_Y, ACC_SCALE_Z };
static uint8_t  mpu_ok = 0;
static uint8_t  restart_event = 0;
static uint32_t recoveries = 0;                      /* times the sensor had to be set up again */

static void mpu_setup(void)                          /* settings the sensor forgets at power loss */
{
    i2c_write_reg(MPU_ADDR, MPU_REG_PWR_MGMT_1, 0x01);       /* wake up, clock from the gyro */
    wait_ms(10);
    i2c_write_reg(MPU_ADDR, MPU_REG_CONFIG, 0x03);           /* low-pass filter about 44 Hz */
    i2c_write_reg(MPU_ADDR, MPU_REG_SMPLRT_DIV, 9);          /* 100 samples per second */
    i2c_write_reg(MPU_ADDR, MPU_REG_GYRO_CONFIG, 0x00);      /* +-250 degrees per second */
    i2c_write_reg(MPU_ADDR, MPU_REG_ACCEL_CONFIG, 0x00);     /* +-2 g */
}

int mpu_init(uint8_t *who)
{
    uint8_t id, v;

    *who = 0;
    mpu_ok = 0;
    if (i2c_read_reg(MPU_ADDR, MPU_REG_WHO_AM_I, &id) != I2C_OK) {
        return 0;
    }
    *who = id;
    i2c_write_reg(MPU_ADDR, MPU_REG_PWR_MGMT_1, 0x80);       /* reset the chip */
    wait_ms(100);
    mpu_setup();
    if (i2c_read_reg(MPU_ADDR, MPU_REG_PWR_MGMT_1, &v) != I2C_OK || (v & MPU_SLEEP_BIT)) {
        return 0;                                            /* still asleep */
    }
    mpu_ok = 1;
    return 1;
}

int mpu_ready(void)
{
    return mpu_ok;
}

static int read_block(uint8_t d[14])
{
    return i2c_read_regs(MPU_ADDR, MPU_REG_ACCEL_XOUT_H, d, 14) == I2C_OK;
}

int mpu_read_raw(int16_t v[7])
{
    uint8_t d[14], pm, any = 0;
    uint8_t i;

    if (!read_block(d)) {
        return MPU_ERR_I2C;
    }
    for (i = 0; i < 14U; i++) {
        any |= d[i];
    }
    /* All 14 bytes zero: the chip probably lost power and is asleep again (its data
       registers reset to 0). Only then check the sleep bit, so a normal read is one
       I2C transfer instead of two. */
    if (any == 0U) {
        if (i2c_read_reg(MPU_ADDR, MPU_REG_PWR_MGMT_1, &pm) != I2C_OK) {
            return MPU_ERR_I2C;
        }
        if (pm & MPU_SLEEP_BIT) {
            mpu_setup();
            recoveries++;
            restart_event = 1;
            if (!read_block(d)) {
                return MPU_ERR_I2C;
            }
        }
    }
    for (i = 0; i < 7U; i++) {                               /* the high byte comes first */
        v[i] = (int16_t)((d[2 * i] << 8) | d[2 * i + 1]);
    }
    return MPU_OK;
}

void mpu_scale(const int16_t v[7], int32_t acc[3], int32_t gyro[3])
{
    uint8_t i;
    for (i = 0; i < 3; i++) {
        acc[i]  = ((int32_t)v[i] - acc_off[i]) * 1000 / acc_scale[i];
        gyro[i] = ((int32_t)v[4 + i] - gyro_off[i]) * 100 / 131;
    }
}

int gyro_zero(void)
{
    int16_t v[7];
    int32_t sum[3] = { 0, 0, 0 };
    uint8_t i, k, good = 0;

    for (i = 0; i < 100U; i++) {
        if (mpu_read_raw(v) == MPU_OK) {
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

uint32_t mpu_restart_count(void)
{
    return recoveries;
}

int mpu_take_restart_event(void)
{
    int e = restart_event;
    restart_event = 0;
    return e;
}

void mpu_force_sleep(void)
{
    i2c_write_reg(MPU_ADDR, MPU_REG_PWR_MGMT_1, 0x80);       /* reset: all settings lost, asleep */
    wait_ms(100);
}
