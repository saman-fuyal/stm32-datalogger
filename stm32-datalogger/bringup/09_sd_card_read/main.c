/*
 * SD card bring-up test: SPI1 driver + SD card in SPI mode, registers only (no HAL).
 * Board: STM32F103C8T6 (Blue Pill), 8 MHz internal clock.
 * This program only READS the card. It never writes, so your files are safe.
 *
 * Pins:  PA9  = UART TX   PA10 = UART RX   (to the CP2102, 9600 baud)
 *        PC13 = LED (on after the card started successfully)
 *        SD module:  CS -> PA4   SCK -> PA5   MISO -> PA6   MOSI -> PA7
 *                    VCC -> 5V   GND -> GND   (see the notes in the message)
 *
 * Commands:  sd init        start the card, print its type and size
 *            sd read N      read sector N and show it (sector 0 is explained)
 *            sd trace on    show every command and answer while starting the card
 *            sd trace off
 *            spiloop        SPI self-test: jumper PA7 to PA6, module disconnected
 *            help
 */
#include <stdint.h>

/* ============================================================== */
/*  Settings                                                      */
/* ============================================================== */

#define UART_BRR_9600   0x341U      /* 8 MHz / 833 = 9604 baud */

/* SPI clock = 8 MHz / 2^(n+1).  The card must be started at 100..400 kHz. */
#define SPI_BR_SLOW     4U          /* 8 MHz / 32 = 250 kHz */
#define SPI_BR_FAST     1U          /* 8 MHz / 4  = 2 MHz. Try 0 (4 MHz) once all works. */

/* ============================================================== */
/*  Registers and bits                                            */
/* ============================================================== */

#define RCC_APB2ENR (*(volatile uint32_t *)0x40021018)
#define RCC_APB2ENR_IOPAEN    (1U << 2)
#define RCC_APB2ENR_IOPCEN    (1U << 4)
#define RCC_APB2ENR_SPI1EN    (1U << 12)
#define RCC_APB2ENR_USART1EN  (1U << 14)

#define GPIOA_CRL   (*(volatile uint32_t *)0x40010800)
#define GPIOA_CRH   (*(volatile uint32_t *)0x40010804)
#define GPIOA_BSRR  (*(volatile uint32_t *)0x40010810)   /* low half sets, high half clears */
#define GPIOC_CRH   (*(volatile uint32_t *)0x40011004)
#define GPIOC_ODR   (*(volatile uint32_t *)0x4001100C)

#define GPIO_IN_FLOATING  0x4U
#define GPIO_IN_PULL      0x8U      /* pull-up if the ODR bit is 1 */
#define GPIO_OUT_PP_2MHZ  0x2U
#define GPIO_AF_PP_50MHZ  0xBU
#define LED_PIN           (1U << 13)
#define SD_CS_PIN         4U        /* PA4 */
#define SPI_MISO_PIN      6U        /* PA6 */

#define SPI1_CR1    (*(volatile uint32_t *)0x40013000)
#define SPI1_SR     (*(volatile uint32_t *)0x40013008)
#define SPI1_DR     (*(volatile uint32_t *)0x4001300C)
#define SPI_CR1_MSTR  (1U << 2)
#define SPI_CR1_SPE   (1U << 6)
#define SPI_CR1_SSI   (1U << 8)
#define SPI_CR1_SSM   (1U << 9)
#define SPI_SR_RXNE   (1U << 0)
#define SPI_SR_TXE    (1U << 1)
#define SPI_SR_BSY    (1U << 7)

#define USART1_SR   (*(volatile uint32_t *)0x40013800)
#define USART1_DR   (*(volatile uint32_t *)0x40013804)
#define USART1_BRR  (*(volatile uint32_t *)0x40013808)
#define USART1_CR1  (*(volatile uint32_t *)0x4001380C)
#define NVIC_ISER1  (*(volatile uint32_t *)0xE000E104)
#define USART_SR_ERRORS   0x07U     /* parity, framing, noise */
#define USART_SR_ORE      (1U << 3)
#define USART_SR_RXNE     (1U << 5)
#define USART_SR_TXE      (1U << 7)
#define USART_CR1_RE      (1U << 2)
#define USART_CR1_TE      (1U << 3)
#define USART_CR1_RXNEIE  (1U << 5)
#define USART_CR1_UE      (1U << 13)
#define NVIC_USART1_BIT   (1U << 5) /* USART1 is interrupt number 37 = 32 + 5 */

#define STK_CTRL    (*(volatile uint32_t *)0xE000E010)
#define STK_LOAD    (*(volatile uint32_t *)0xE000E014)
#define STK_VAL     (*(volatile uint32_t *)0xE000E018)
#define STK_RELOAD_1MS  (8000U - 1U)
#define STK_CTRL_RUN    7U          /* enable, interrupt on, CPU clock */

/* ============================================================== */
/*  Pins, LED and millisecond clock                               */
/* ============================================================== */

/* slot = pin number inside its register (0..7 for CRL, pin - 8 for CRH) */
static void gpio_config(volatile uint32_t *cr, uint8_t slot, uint32_t mode)
{
    *cr = (*cr & ~(0xFU << (4U * slot))) | (mode << (4U * slot));
}

static void led_on(void)  { GPIOC_ODR &= ~LED_PIN; }     /* the LED is active-low */
static void led_off(void) { GPIOC_ODR |= LED_PIN; }

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

/* ============================================================== */
/*  UART receive: interrupt fills a ring buffer, main reads it    */
/* ============================================================== */

#define RX_SIZE 64U                 /* must be a power of two */
static volatile uint8_t rx_head = 0;
static volatile uint8_t rx_tail = 0;
static volatile uint8_t rx_buf[RX_SIZE];

void USART1_IRQHandler(void)
{
    uint32_t sr = USART1_SR;
    if (sr & (USART_SR_RXNE | USART_SR_ORE)) {
        uint8_t c = (uint8_t)USART1_DR;               /* reading DR clears the flags */
        if (!(sr & USART_SR_ERRORS)) {                /* drop bytes with line errors */
            uint8_t next = (rx_head + 1U) & (RX_SIZE - 1U);
            if (next != rx_tail) {
                rx_buf[rx_head] = c;
                rx_head = next;
            }
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

static void rx_flush(void)
{
    rx_tail = rx_head;              /* forget anything typed while a command was running */
}

/* ============================================================== */
/*  UART transmit and printing                                    */
/* ============================================================== */

static void uart_send_char(char c)
{
    while (!(USART1_SR & USART_SR_TXE)) { }
    USART1_DR = (uint32_t)(uint8_t)c;
}

static void uart_send_string(const char *s)
{
    while (*s) {
        uart_send_char(*s++);
    }
}

static void uart_send_hex8(uint8_t v)
{
    static const char digits[] = "0123456789ABCDEF";
    uart_send_char(digits[v >> 4]);
    uart_send_char(digits[v & 0xFU]);
}

static void uart_send_hex32(uint32_t v)
{
    uart_send_hex8((uint8_t)(v >> 24));
    uart_send_hex8((uint8_t)(v >> 16));
    uart_send_hex8((uint8_t)(v >> 8));
    uart_send_hex8((uint8_t)v);
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

/* ============================================================== */
/*  Text helpers                                                  */
/* ============================================================== */

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

static uint8_t str_len(const char *s)
{
    uint8_t n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

static int parse_u32(const char *s, uint32_t *out)
{
    uint32_t v = 0;
    if (*s == '\0') {
        return 0;
    }
    while (*s) {
        if (*s < '0' || *s > '9' || v > 429496728U) {      /* not a digit, or would overflow */
            return 0;
        }
        v = v * 10U + (uint32_t)(*s - '0');
        s++;
    }
    *out = v;
    return 1;
}

/* ============================================================== */
/*  SPI1 (master, mode 0, 8 bit, chip select on PA4 by hand)      */
/* ============================================================== */

static void spi_set_speed(uint8_t br)
{
    uint32_t t = 100000U;
    while ((SPI1_SR & SPI_SR_BSY) && --t) { }
    SPI1_CR1 = 0;                                                 /* off, so BR can change */
    SPI1_CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI | ((uint32_t)br << 3);
    SPI1_CR1 |= SPI_CR1_SPE;
}

/* send one byte and return the byte that arrived at the same time */
static uint8_t spi_xfer(uint8_t out)
{
    uint32_t t = 100000U;
    while (!(SPI1_SR & SPI_SR_TXE)) {
        if (--t == 0) { return 0xFF; }
    }
    SPI1_DR = out;
    t = 100000U;
    while (!(SPI1_SR & SPI_SR_RXNE)) {
        if (--t == 0) { return 0xFF; }
    }
    return (uint8_t)SPI1_DR;
}

static void cs_high(void) { GPIOA_BSRR = (1U << SD_CS_PIN); }
static void cs_low(void)  { GPIOA_BSRR = (1U << (SD_CS_PIN + 16U)); }

/* ============================================================== */
/*  SD card driver (SPI mode)                                     */
/*  Needs from above: spi_xfer, spi_set_speed, cs_high, cs_low,   */
/*  ms_ticks and sd_trace.                                        */
/* ============================================================== */

static uint8_t sd_trace_on = 0;

static void sd_trace(const char *name, uint32_t arg, uint8_t r1)
{
    if (!sd_trace_on) {
        return;
    }
    uart_send_string("  ");
    uart_send_string(name);
    uart_send_string(" arg=");
    uart_send_hex32(arg);
    uart_send_string(" -> 0x");
    uart_send_hex8(r1);
    uart_send_string("\r\n");
}

/* BEGIN SD DRIVER */
#define SD_CMD0    0U       /* go idle */
#define SD_CMD8    8U       /* check voltage, tells v2 cards apart */
#define SD_CMD9    9U       /* read the CSD register (size) */
#define SD_CMD16   16U      /* set block length */
#define SD_CMD17   17U      /* read one block */
#define SD_CMD55   55U      /* next command is an application command */
#define SD_CMD58   58U      /* read the OCR register */
#define SD_ACMD41  (0x80U | 41U)   /* start the card (the 0x80 means: send CMD55 first) */

#define SD_NONE       0U
#define SD_V1         1U    /* old card, byte addressing */
#define SD_V2         2U    /* standard capacity, byte addressing */
#define SD_V2_BLOCK   3U    /* SDHC / SDXC, block addressing (an 8 GB card is this one) */

#define SD_OK                0
#define SD_ERR_NO_CARD       1   /* CMD0 got no answer at all */
#define SD_ERR_CMD0          2   /* CMD0 answered, but not with 0x01 */
#define SD_ERR_VOLTAGE       3   /* CMD8 answered with the wrong pattern */
#define SD_ERR_INIT_TIMEOUT  4   /* ACMD41 never finished */
#define SD_ERR_UNSUPPORTED   5   /* not an SD card (for example an MMC card) */
#define SD_ERR_CMD58         6
#define SD_ERR_NOT_INIT      7
#define SD_ERR_READ_CMD      8
#define SD_ERR_READ_TOKEN    9
#define SD_ERR_BUSY         11   /* card holds MISO low for too long */

#define SD_BLOCK_SIZE  512U

static uint8_t  sd_type = SD_NONE;
static uint32_t sd_sectors = 0;        /* card size in 512-byte sectors, from the CSD */

static void sd_deselect(void)
{
    cs_high();
    (void)spi_xfer(0xFF);              /* gives the card 8 more clocks to finish */
}

static int sd_wait_ready(uint32_t timeout_ms)      /* the card holds MISO low while busy */
{
    uint32_t t0 = ms_ticks;
    do {
        if (spi_xfer(0xFF) == 0xFF) {
            return 1;
        }
    } while ((ms_ticks - t0) < timeout_ms);
    return 0;
}

static int sd_select(void)
{
    cs_low();
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
   The 2 CRC bytes after the data are read and ignored: in SPI mode the card may send
   anything there (yours sends 0000), and FatFs ignores them too. */
static int sd_read_data(uint8_t *buf, uint16_t len)
{
    uint32_t t0 = ms_ticks;
    uint16_t i;
    uint8_t r;

    do {
        r = spi_xfer(0xFF);
    } while (r == 0xFF && (ms_ticks - t0) < 200U);
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

/* Starts the card. Returns SD_OK or one of the SD_ERR_ codes. */
static int sd_init(void)
{
    uint8_t r, i, ocr[4], csd[16];
    uint32_t t0;

    sd_type = SD_NONE;
    sd_sectors = 0;
    spi_set_speed(SPI_BR_SLOW);
    cs_high();
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
        t0 = ms_ticks;
        do {                                   /* ACMD41 with HCS: "I can handle SDHC cards" */
            r = sd_send_cmd(SD_ACMD41, 0x40000000UL);
        } while (r != 0U && (ms_ticks - t0) < 1000U);
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
        t0 = ms_ticks;
        do {
            r = sd_send_cmd(SD_ACMD41, 0);
        } while (r != 0U && r <= 1U && (ms_ticks - t0) < 1000U);
        sd_trace("ACMD41", 0, r);
        if (r > 1U) { sd_deselect(); return SD_ERR_UNSUPPORTED; }
        if (r != 0U) { sd_deselect(); return SD_ERR_INIT_TIMEOUT; }
        sd_type = SD_V1;
    }

    if (sd_type != SD_V2_BLOCK) {              /* byte-addressed cards: make the block size 512 */
        r = sd_send_cmd(SD_CMD16, SD_BLOCK_SIZE);
        sd_trace("CMD16", SD_BLOCK_SIZE, r);
        if (r != 0U) { sd_deselect(); sd_type = SD_NONE; return SD_ERR_CMD58; }
    }

    r = sd_send_cmd(SD_CMD9, 0);               /* CSD register: card size */
    sd_trace("CMD9 ", 0, r);
    if (r == 0U && sd_read_data(csd, 16) == SD_OK) {
        sd_sectors = sd_sectors_from_csd(csd);
    }
    sd_deselect();

    spi_set_speed(SPI_BR_FAST);                /* init is done: run at full speed */
    return SD_OK;
}

/* Reads one 512-byte sector. Returns SD_OK or an SD_ERR_ code. */
static int sd_read_block(uint32_t sector, uint8_t *buf)
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
/* END SD DRIVER */

static const char *sd_error_text(int code)
{
    switch (code) {
    case SD_ERR_NO_CARD:      return "no answer from the card (0xFF). Check power (5V?), the card in the slot, and the 4 signal wires, especially CS and MISO";
    case SD_ERR_CMD0:         return "card answered CMD0 with something odd. MISO may be stuck low, or the wires are noisy";
    case SD_ERR_VOLTAGE:      return "CMD8 answer wrong. Supply voltage is probably too low";
    case SD_ERR_INIT_TIMEOUT: return "card did not finish starting within 1 s. Weak supply, or a damaged card";
    case SD_ERR_UNSUPPORTED:  return "not an SD card this program supports";
    case SD_ERR_CMD58:        return "OCR or block-size command failed";
    case SD_ERR_NOT_INIT:     return "card not started yet, type: sd init";
    case SD_ERR_READ_CMD:     return "read command refused (sector number too big?)";
    case SD_ERR_READ_TOKEN:   return "card never sent the data. Try again, or lower the SPI speed";
    case SD_ERR_BUSY:         return "card stays busy";
    default:                  return "unknown error";
    }
}

/* ============================================================== */
/*  Showing sectors                                               */
/* ============================================================== */

static uint8_t sector_buf[SD_BLOCK_SIZE];

/* prints len bytes starting at offset, 16 per line, with the text next to them */
static void print_hexdump(const uint8_t *b, uint16_t offset, uint16_t len)
{
    uint16_t i, j;
    for (i = 0; i < len; i += 16U) {
        uart_send_hex8((uint8_t)((offset + i) >> 8));
        uart_send_hex8((uint8_t)(offset + i));
        uart_send_string(": ");
        for (j = 0; j < 16U; j++) {
            uart_send_hex8(b[offset + i + j]);
            uart_send_char(' ');
        }
        uart_send_char(' ');
        for (j = 0; j < 16U; j++) {
            char c = (char)b[offset + i + j];
            uart_send_char((c >= 32 && c < 127) ? c : '.');
        }
        uart_send_string("\r\n");
    }
}

static void describe_sector0(const uint8_t *b)
{
    uint32_t lba, size;

    if (b[510] != 0x55U || b[511] != 0xAAU) {
        uart_send_string("No 55 AA signature at the end: sector 0 is not a boot sector.\r\n"
                         "Either the card is unformatted, or the read is wrong.\r\n");
        return;
    }
    uart_send_string("Signature 55 AA found. ");
    if (b[0] == 0xEBU || b[0] == 0xE9U) {
        uart_send_string("This is a FAT boot sector (the card has no partition table).\r\n");
        return;
    }
    lba  = (uint32_t)b[454] | ((uint32_t)b[455] << 8) | ((uint32_t)b[456] << 16) | ((uint32_t)b[457] << 24);
    size = (uint32_t)b[458] | ((uint32_t)b[459] << 8) | ((uint32_t)b[460] << 16) | ((uint32_t)b[461] << 24);
    uart_send_string("This is an MBR.\r\nPartition 1: type 0x");
    uart_send_hex8(b[450]);
    if (b[450] == 0x0BU || b[450] == 0x0CU) {
        uart_send_string(" (FAT32)");
    } else if (b[450] == 0x06U || b[450] == 0x0EU) {
        uart_send_string(" (FAT16)");
    } else if (b[450] == 0x07U) {
        uart_send_string(" (exFAT or NTFS: reformat as FAT32 for the logger)");
    }
    uart_send_string(", starts at sector ");
    uart_send_number(lba);
    uart_send_string(", size ");
    uart_send_number(size / 2048U);
    uart_send_string(" MB\r\n");
}

/* ============================================================== */
/*  Commands                                                      */
/* ============================================================== */

static void print_error(int code)
{
    uart_send_string("ERR ");
    uart_send_number((uint32_t)code);
    uart_send_string(": ");
    uart_send_string(sd_error_text(code));
    uart_send_string("\r\n");
}

static void cmd_sd_init(const char *arg)
{
    int r;
    (void)arg;
    led_off();
    r = sd_init();
    if (r != SD_OK) {
        print_error(r);
        return;
    }
    led_on();
    uart_send_string("Card started: ");
    uart_send_string(sd_type == SD_V2_BLOCK ? "SDHC/SDXC (block addressing)" :
                     sd_type == SD_V2       ? "SD version 2 (byte addressing)" :
                                              "SD version 1 (byte addressing)");
    uart_send_string("\r\n");
    if (sd_sectors != 0U) {
        uart_send_string("Size: ");
        uart_send_number(sd_sectors / 2048U);
        uart_send_string(" MB (");
        uart_send_number(sd_sectors);
        uart_send_string(" sectors)\r\n");
    } else {
        uart_send_string("Size: could not be read\r\n");
    }
}

static void cmd_sd_read(const char *arg)
{
    uint32_t sector;
    int r;

    if (!parse_u32(arg, &sector)) {
        uart_send_string("ERR use: sd read N   (N = sector number, 0 = first sector)\r\n");
        return;
    }
    r = sd_read_block(sector, sector_buf);
    if (r != SD_OK) {
        print_error(r);
        return;
    }
    uart_send_string("Sector ");
    uart_send_number(sector);
    uart_send_string(", first 64 bytes:\r\n");
    print_hexdump(sector_buf, 0, 64);
    uart_send_string("last 64 bytes:\r\n");
    print_hexdump(sector_buf, 448, 64);
    if (sector == 0U) {
        describe_sector0(sector_buf);
    }
}

static void cmd_sd_trace_on(const char *arg)  { (void)arg; sd_trace_on = 1; uart_send_string("OK trace on\r\n"); }
static void cmd_sd_trace_off(const char *arg) { (void)arg; sd_trace_on = 0; uart_send_string("OK trace off\r\n"); }

/* Jumper PA7 (MOSI) to PA6 (MISO) and unplug the module: every byte sent must come back. */
static void cmd_spiloop(const char *arg)
{
    static const uint8_t pattern[] = { 0xA5, 0x5A, 0x00, 0xFF, 0x3C, 0xC3, 0x01, 0x80 };
    uint8_t br, k;
    uint8_t bad;
    (void)arg;

    cs_high();
    for (br = 0; br < 2U; br++) {
        uint8_t speed = br ? SPI_BR_FAST : SPI_BR_SLOW;
        spi_set_speed(speed);
        bad = 0;
        for (k = 0; k < sizeof(pattern); k++) {
            if (spi_xfer(pattern[k]) != pattern[k]) {
                bad++;
            }
        }
        uart_send_string(br ? "fast clock: " : "slow clock: ");
        uart_send_number(sizeof(pattern) - bad);
        uart_send_string(" of ");
        uart_send_number(sizeof(pattern));
        uart_send_string(" bytes came back\r\n");
    }
    uart_send_string("All 8 on both = the SPI peripheral and pins PA6/PA7 work.\r\n"
                     "0xFF only = no jumper (the pull-up on PA6 answers).\r\n");
}

static void cmd_help(const char *arg);

typedef struct {
    const char *name;
    void      (*run)(const char *arg);
} command_t;

static const command_t commands[] = {
    { "sd init",      cmd_sd_init      },
    { "sd trace on",  cmd_sd_trace_on  },
    { "sd trace off", cmd_sd_trace_off },
    { "spiloop",      cmd_spiloop      },
    { "help",         cmd_help         },
};
#define NUM_COMMANDS (sizeof(commands) / sizeof(commands[0]))

static void cmd_help(const char *arg)
{
    (void)arg;
    uart_send_string("sd init        start the card, print type and size\r\n");
    uart_send_string("sd read N      read sector N (sector 0 is explained)\r\n");
    uart_send_string("sd trace on    show every card command and answer (and: sd trace off)\r\n");
    uart_send_string("spiloop        SPI self-test with PA7 jumpered to PA6, module unplugged\r\n");
}

static void handle_line(const char *cmd)
{
    uint8_t i;

    if (cmd[0] == '\0') {
        return;
    }
    if (starts_with(cmd, "sd read ")) {
        cmd_sd_read(cmd + str_len("sd read "));
        return;
    }
    for (i = 0; i < NUM_COMMANDS; i++) {
        if (str_equal(cmd, commands[i].name)) {
            commands[i].run("");
            return;
        }
    }
    uart_send_string("ERR unknown command (type help)\r\n");
}

/* ============================================================== */
/*  Console and start-up                                          */
/* ============================================================== */

#define LINE_SIZE 24U
static char    line[LINE_SIZE];
static uint8_t line_len = 0;

static void console_char(uint8_t c)
{
    if (c == '\r' || c == '\n') {
        uart_send_string("\r\n");
        line[line_len] = '\0';
        handle_line(line);
        rx_flush();
        line_len = 0;
        uart_send_string("> ");
    } else if (c == 0x08 || c == 0x7F) {             /* backspace */
        if (line_len > 0) {
            line_len--;
            uart_send_string("\b \b");
        }
    } else if (line_len < LINE_SIZE - 1U) {
        line[line_len++] = (char)c;
        uart_send_char((char)c);                     /* echo */
    }
}

static void hardware_init(void)
{
    RCC_APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPCEN |
                   RCC_APB2ENR_SPI1EN | RCC_APB2ENR_USART1EN;

    cs_high();                                       /* CS high before the pin becomes an output */
    gpio_config(&GPIOA_CRL, SD_CS_PIN, GPIO_OUT_PP_2MHZ);   /* PA4 = CS */
    gpio_config(&GPIOA_CRL, 5, GPIO_AF_PP_50MHZ);           /* PA5 = SCK */
    GPIOA_BSRR = (1U << SPI_MISO_PIN);                      /* pull-up on MISO: no card reads 0xFF */
    gpio_config(&GPIOA_CRL, SPI_MISO_PIN, GPIO_IN_PULL);    /* PA6 = MISO */
    gpio_config(&GPIOA_CRL, 7, GPIO_AF_PP_50MHZ);           /* PA7 = MOSI */

    gpio_config(&GPIOA_CRH, 1, GPIO_AF_PP_50MHZ);           /* PA9  = UART TX */
    gpio_config(&GPIOA_CRH, 2, GPIO_IN_FLOATING);           /* PA10 = UART RX */
    gpio_config(&GPIOC_CRH, 5, GPIO_OUT_PP_2MHZ);           /* PC13 = LED */
    led_off();

    USART1_BRR = UART_BRR_9600;
    USART1_CR1 = USART_CR1_UE | USART_CR1_RXNEIE | USART_CR1_TE | USART_CR1_RE;
    NVIC_ISER1 = NVIC_USART1_BIT;

    STK_LOAD = STK_RELOAD_1MS;
    STK_VAL  = 0;
    STK_CTRL = STK_CTRL_RUN;

    spi_set_speed(SPI_BR_SLOW);
}

int main(void)
{
    uint8_t c;

    hardware_init();
    uart_send_string("\r\nSD card test (read only). Type help.\r\n> ");

    while (1) {
        if (rx_pop(&c)) {
            console_char(c);
        }
    }
}
