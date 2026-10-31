/* SD card in SPI mode: start-up, read and write single 512-byte sectors. */
#ifndef SD_H
#define SD_H

#include <stdint.h>

#define SD_BLOCK_SIZE  512U

/* card types */
#define SD_NONE       0U
#define SD_V1         1U    /* old card, byte addressing */
#define SD_V2         2U    /* standard capacity, byte addressing */
#define SD_V2_BLOCK   3U    /* SDHC / SDXC, block addressing */

/* results */
#define SD_OK                0
#define SD_ERR_NO_CARD       1   /* CMD0 got no answer at all */
#define SD_ERR_CMD0          2   /* CMD0 answered, but not with 0x01 */
#define SD_ERR_VOLTAGE       3   /* CMD8 answered with the wrong pattern */
#define SD_ERR_INIT_TIMEOUT  4   /* ACMD41 never finished */
#define SD_ERR_UNSUPPORTED   5   /* not an SD card (for example an MMC card) */
#define SD_ERR_CMD58         6   /* reading the OCR register failed */
#define SD_ERR_NOT_INIT      7
#define SD_ERR_READ_CMD      8
#define SD_ERR_READ_TOKEN    9
#define SD_ERR_CMD16        10   /* setting the block size failed */
#define SD_ERR_BUSY         11   /* card holds MISO low for too long */
#define SD_ERR_WRITE_CMD    12   /* write command refused */
#define SD_ERR_WRITE_DATA   13   /* card rejected the data (CRC or write error) */

/* optional: called after each start-up command (for 'sd trace'); NULL = off */
typedef void (*sd_trace_fn)(const char *name, uint32_t arg, uint8_t r1);
void        sd_set_trace(sd_trace_fn fn);

int         sd_init(void);                               /* SD_OK or an SD_ERR_ code */
int         sd_read_block(uint32_t sector, uint8_t *buf);
int         sd_write_block(uint32_t sector, const uint8_t *buf);
int         sd_sync(void);                               /* waits until the card is not busy */
uint8_t     sd_card_type(void);
uint32_t    sd_sector_count(void);                        /* card size in 512-byte sectors */
const char *sd_error_text(int code);

#endif
