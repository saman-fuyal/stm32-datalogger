/* SD card driver (SPI mode). Start-up and read come from bringup/09_sd_card_read. */
#include "board.h"
#include "sd.h"
#include "spi.h"

#define SD_CMD0    0U       /* go idle */
#define SD_CMD8    8U       /* check voltage, tells v2 cards apart */
#define SD_CMD9    9U       /* read the CSD register (size) */
#define SD_CMD16   16U      /* set block length */
#define SD_CMD17   17U      /* read one block */
#define SD_CMD24   24U      /* write one block */
#define SD_CMD55   55U      /* next command is an application command */
#define SD_CMD58   58U      /* read the OCR register */
#define SD_ACMD41  (0x80U | 41U)   /* start the card (the 0x80 means: send CMD55 first) */

static uint8_t     sd_type = SD_NONE;
static uint32_t    sd_sectors = 0;
static sd_trace_fn trace = 0;

void sd_set_trace(sd_trace_fn fn)
{
    trace = fn;
}

static void sd_trace(const char *name, uint32_t arg, uint8_t r1)
{
    if (trace) {
        trace(name, arg, r1);
    }
}

static void sd_deselect(void)
{
    spi_cs_high();
    (void)spi_xfer(0xFF);              /* gives the card 8 more clocks to finish */
}

static int sd_wait_ready(uint32_t timeout_ms)      /* the card holds MISO low while busy */
{
    uint32_t t0 = tick_ms();
    do {
        if (spi_xfer(0xFF) == 0xFF) {
            return 1;
        }
        if ((tick_ms() - t0) >= 2U) {
            wait_ms(1);                 /* a long write (up to 250 ms): let other tasks run */
        }
    } while ((tick_ms() - t0) < timeout_ms);
    return 0;
}

static int sd_select(void)
{
    spi_cs_low();
    (void)spi_xfer(0xFF);
    if (sd_wait_ready(500U)) {
        return 1;
    }
    sd_deselect();
    return 0;
}

/* Sends a command and returns the R1 answer byte (0xFF = no answer).
   Chip select stays low afterwards so the caller can read more bytes. */
static uint8_t sd_send_cmd(uint8_t cmd, uint32_t arg)
{
    uint8_t r, crc, n;

    if (cmd & 0x80U) {                         /* application command: CMD55 first */
        cmd &= 0x7FU;
        r = sd_send_cmd(SD_CMD55, 0);
        if (r > 1U) {
            return r;
        }
    }
    sd_deselect();
    if (!sd_select()) {
        return 0xFF;
    }
    crc = 0x01;                                /* only CMD0 and CMD8 need a real CRC */
    if (cmd == SD_CMD0) { crc = 0x95; }
    if (cmd == SD_CMD8) { crc = 0x87; }

    (void)spi_xfer((uint8_t)(0x40U | cmd));
    (void)spi_xfer((uint8_t)(arg >> 24));
    (void)spi_xfer((uint8_t)(arg >> 16));
    (void)spi_xfer((uint8_t)(arg >> 8));
    (void)spi_xfer((uint8_t)arg);
    (void)spi_xfer(crc);

    n = 10;                                    /* the answer comes within 8 bytes */
    do {
        r = spi_xfer(0xFF);
    } while ((r & 0x80U) && --n);
    return r;
}

/* After a command that returned R1 = 0: wait for the data token, then read len bytes.
   The 2 CRC bytes after the data are read and ignored (FatFs ignores them too). */
static int sd_read_data(uint8_t *buf, uint16_t len)
{
    uint32_t t0 = tick_ms();
    uint16_t i;
    uint8_t r;

    do {
        r = spi_xfer(0xFF);
    } while (r == 0xFF && (tick_ms() - t0) < 200U);
    if (r != 0xFE) {                           /* 0xFE = "data follows" */
        return SD_ERR_READ_TOKEN;
    }
    for (i = 0; i < len; i++) {
        buf[i] = spi_xfer(0xFF);
    }
    (void)spi_xfer(0xFF);                      /* CRC, high byte (ignored) */
    (void)spi_xfer(0xFF);                      /* CRC, low byte (ignored) */
    return SD_OK;
}

/* card size in sectors from the 16-byte CSD register */
static uint32_t sd_sectors_from_csd(const uint8_t csd[16])
{
    uint32_t c_size, mult, read_bl_len;

    if ((csd[0] >> 6) == 1U) {                 /* CSD version 2 (SDHC, SDXC) */
        c_size = ((uint32_t)(csd[7] & 0x3FU) << 16) | ((uint32_t)csd[8] << 8) | csd[9];
        return (c_size + 1U) * 1024U;          /* (C_SIZE + 1) * 512 KiB */
    }
    read_bl_len = csd[5] & 0x0FU;              /* version 1 (older cards) */
    c_size = ((uint32_t)(csd[6] & 0x03U) << 10) | ((uint32_t)csd[7] << 2) | (csd[8] >> 6);
    mult   = ((uint32_t)(csd[9] & 0x03U) << 1) | (csd[10] >> 7);
    return ((c_size + 1U) << (mult + 2U)) << (read_bl_len - 9U);
}

int sd_init(void)
{
    uint8_t r = 0xFF, i, ocr[4], csd[16];
    uint32_t t0;

    sd_type = SD_NONE;
    sd_sectors = 0;
    spi_set_speed(SPI_BR_SLOW);
    spi_cs_high();
    for (i = 0; i < 10U; i++) {                /* 80 clocks with CS high wakes the card up */
        (void)spi_xfer(0xFF);
    }

    for (i = 0; i < 3U; i++) {                 /* CMD0: go to idle state, answer must be 0x01 */
        r = sd_send_cmd(SD_CMD0, 0);
        sd_trace("CMD0 ", 0, r);
        if (r == 0x01U) {
            break;
        }
        sd_deselect();
        wait_ms(10);
    }
    if (r == 0xFFU) { sd_deselect(); return SD_ERR_NO_CARD; }
    if (r != 0x01U) { sd_deselect(); return SD_ERR_CMD0; }

    r = sd_send_cmd(SD_CMD8, 0x000001AAU);     /* 2.7-3.6 V, check pattern 0xAA */
    sd_trace("CMD8 ", 0x1AAU, r);
    if (r == 0x01U) {                          /* version 2 card */
        for (i = 0; i < 4U; i++) {
            ocr[i] = spi_xfer(0xFF);
        }
        if (ocr[2] != 0x01U || ocr[3] != 0xAAU) {
            sd_deselect();
            return SD_ERR_VOLTAGE;
        }
        t0 = tick_ms();
        do {                                   /* ACMD41 with HCS: "I can handle SDHC cards" */
            r = sd_send_cmd(SD_ACMD41, 0x40000000UL);
        } while (r != 0U && (tick_ms() - t0) < 1000U);
        sd_trace("ACMD41", 0x40000000UL, r);
        if (r != 0U) { sd_deselect(); return SD_ERR_INIT_TIMEOUT; }

        r = sd_send_cmd(SD_CMD58, 0);          /* read OCR: bit 30 = block addressing */
        sd_trace("CMD58", 0, r);
        if (r != 0U) { sd_deselect(); return SD_ERR_CMD58; }
        for (i = 0; i < 4U; i++) {
            ocr[i] = spi_xfer(0xFF);
        }
        sd_type = (ocr[0] & 0x40U) ? SD_V2_BLOCK : SD_V2;
    } else {                                   /* illegal command: a version 1 card */
        t0 = tick_ms();
        do {
            r = sd_send_cmd(SD_ACMD41, 0);
        } while (r != 0U && r <= 1U && (tick_ms() - t0) < 1000U);
        sd_trace("ACMD41", 0, r);
        if (r > 1U) { sd_deselect(); return SD_ERR_UNSUPPORTED; }
        if (r != 0U) { sd_deselect(); return SD_ERR_INIT_TIMEOUT; }
        sd_type = SD_V1;
    }

    if (sd_type != SD_V2_BLOCK) {              /* byte-addressed cards: make the block size 512 */
        r = sd_send_cmd(SD_CMD16, SD_BLOCK_SIZE);
        sd_trace("CMD16", SD_BLOCK_SIZE, r);
        if (r != 0U) { sd_deselect(); sd_type = SD_NONE; return SD_ERR_CMD16; }
    }

    r = sd_send_cmd(SD_CMD9, 0);               /* CSD register: card size */
    sd_trace("CMD9 ", 0, r);
    if (r == 0U && sd_read_data(csd, 16) == SD_OK) {
        sd_sectors = sd_sectors_from_csd(csd);
    }
    sd_deselect();

    spi_set_speed(SPI_BR_FAST);                /* start-up is done: run at full speed */
    return SD_OK;
}

int sd_read_block(uint32_t sector, uint8_t *buf)
{
    uint8_t r;
    int res;

    if (sd_type == SD_NONE) {
        return SD_ERR_NOT_INIT;
    }
    if (sd_type != SD_V2_BLOCK) {
        sector *= SD_BLOCK_SIZE;               /* byte addressing */
    }
    r = sd_send_cmd(SD_CMD17, sector);
    if (r != 0U) {
        sd_deselect();
        return SD_ERR_READ_CMD;
    }
    res = sd_read_data(buf, SD_BLOCK_SIZE);
    sd_deselect();
    return res;
}

int sd_write_block(uint32_t sector, const uint8_t *buf)
{
    uint16_t i;

    if (sd_type == SD_NONE) {
        return SD_ERR_NOT_INIT;
    }
    if (sd_type != SD_V2_BLOCK) {
        sector *= SD_BLOCK_SIZE;               /* byte addressing */
    }
    if (sd_send_cmd(SD_CMD24, sector) != 0U) {
        sd_deselect();
        return SD_ERR_WRITE_CMD;
    }
    (void)spi_xfer(0xFF);                      /* one gap byte */
    (void)spi_xfer(0xFE);                      /* start token: data follows */
    for (i = 0; i < SD_BLOCK_SIZE; i++) {
        (void)spi_xfer(buf[i]);
    }
    (void)spi_xfer(0xFF);                      /* CRC, not checked in SPI mode */
    (void)spi_xfer(0xFF);
    if ((spi_xfer(0xFF) & 0x1FU) != 0x05U) {   /* data response: xxx0 0101 = accepted */
        sd_deselect();
        return SD_ERR_WRITE_DATA;
    }
    if (!sd_wait_ready(500U)) {                /* the card holds MISO low while it programs */
        sd_deselect();
        return SD_ERR_BUSY;
    }
    sd_deselect();
    return SD_OK;
}

int sd_sync(void)
{
    int ok;
    if (sd_type == SD_NONE) {
        return SD_ERR_NOT_INIT;
    }
    spi_cs_low();
    ok = sd_wait_ready(500U);
    sd_deselect();
    return ok ? SD_OK : SD_ERR_BUSY;
}

uint8_t sd_card_type(void)
{
    return sd_type;
}

uint32_t sd_sector_count(void)
{
    return sd_sectors;
}

const char *sd_error_text(int code)
{
    switch (code) {
    case SD_ERR_NO_CARD:      return "no answer from the card (0xFF). Check power (5V?), the card in the slot, and the 4 signal wires, especially CS and MISO";
    case SD_ERR_CMD0:         return "card answered CMD0 with something odd. MISO may be stuck low, or the wires are noisy";
    case SD_ERR_VOLTAGE:      return "CMD8 answer wrong. Supply voltage is probably too low";
    case SD_ERR_INIT_TIMEOUT: return "card did not finish starting within 1 s. Weak supply, or a damaged card";
    case SD_ERR_UNSUPPORTED:  return "not an SD card this program supports";
    case SD_ERR_CMD58:        return "reading the OCR register failed";
    case SD_ERR_NOT_INIT:     return "card not started yet, type: sd init";
    case SD_ERR_READ_CMD:     return "read command refused (sector number too big?)";
    case SD_ERR_READ_TOKEN:   return "card never sent the data. Try again, or lower the SPI speed";
    case SD_ERR_CMD16:        return "setting the 512-byte block size failed";
    case SD_ERR_BUSY:         return "card stays busy";
    case SD_ERR_WRITE_CMD:    return "write command refused (card removed? write-protected?)";
    case SD_ERR_WRITE_DATA:   return "card rejected the data. Supply dip during the write?";
    default:                  return "unknown error";
    }
}
