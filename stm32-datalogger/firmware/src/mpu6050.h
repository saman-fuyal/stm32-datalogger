/* MPU6050 acceleration and rotation, +-2 g and +-250 deg/s, 100 samples/s. */
#ifndef MPU6050_H
#define MPU6050_H

#include <stdint.h>

#define MPU_OK      0
#define MPU_ERR_I2C 1

/* who = WHO_AM_I value, or 0 if the chip did not answer. Returns 1 = ready. */
int      mpu_init(uint8_t *who);
int      mpu_ready(void);
/* v[0..2] = acceleration X,Y,Z   v[3] = temperature   v[4..6] = gyro X,Y,Z */
int      mpu_read_raw(int16_t v[7]);
/* raw counts -> thousandths of a g, and hundredths of a degree per second */
void     mpu_scale(const int16_t v[7], int32_t acc[3], int32_t gyro[3]);
int      gyro_zero(void);               /* the sensor must be still for 1 s. 1 = ok */
uint32_t mpu_restart_count(void);
int      mpu_take_restart_event(void);  /* 1 once after the chip had to be set up again */
void     mpu_force_sleep(void);         /* test only: acts like a power loss of the MPU */

#endif
