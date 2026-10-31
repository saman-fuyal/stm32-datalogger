/* BMP280 temperature and pressure (Bosch integer formulas, forced mode). */
#ifndef BMP280_H
#define BMP280_H

#include <stdint.h>

/* read results. The numbers are the ones the logger has always printed ("code 3"). */
enum {
    BMP_OK         = 0,
    BMP_ERR_START  = 1,     /* could not start a measurement */
    BMP_ERR_READ   = 3,     /* I2C read failed */
    BMP_ERR_NODATA = 4,     /* sensor returned the 'no data yet' value */
    BMP_ERR_RANGE  = 5      /* impossible value, rejected */
};
#define BMP_ERR_COUNT 6     /* size for an array indexed by the codes above */

int         bmp_init(void);                                 /* 1 = ready */
int         bmp_ready(void);
int         bmp_read_once(int32_t *temp_c100, uint32_t *press_pa);   /* BMP_OK or code */
int         bmp_read(int32_t *temp_c100, uint32_t *press_pa);        /* one automatic retry */
uint32_t    bmp_retry_count(void);
const char *bmp_err_text(int code);
/* runs the formulas on the datasheet example; the chip's own constants are kept */
void        bmp_selftest(int32_t *temp_c100, uint32_t *press_pa);

#endif
