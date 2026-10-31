/* I2C1 master, 100 kHz, polling, every wait has a timeout. */
#ifndef I2C_H
#define I2C_H

#include <stdint.h>

#define I2C_OK          0
#define I2C_ERR_NACK    1       /* nobody answered at that address */
#define I2C_ERR_TIMEOUT 2       /* the bus got stuck; the peripheral was reset */

void i2c_init(void);
int  i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *value);
int  i2c_write_reg(uint8_t addr, uint8_t reg, uint8_t value);
int  i2c_read_regs(uint8_t addr, uint8_t reg, uint8_t *buf, uint8_t len);   /* len >= 3 */
int  i2c_probe(uint8_t addr);   /* 1 if a device answers at this address */
uint32_t i2c_recovery_count(void);  /* times SDA was stuck low and the bus was freed */

#endif
