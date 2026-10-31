/*
 * Text output and small text helpers.
 *
 * Every print function writes into an outbuf_t:
 *   - an outbuf_t with a buffer collects the text in memory (used to build a log line,
 *     so its CRC can be computed and the same line can go to the UART and the SD card);
 *   - the special outbuf 'CON' has no buffer and sends every character straight to the UART.
 */
#ifndef FMT_H
#define FMT_H

#include <stdint.h>

typedef struct {
    char    *buf;           /* NULL = send to the UART */
    uint16_t len;           /* characters stored so far (buf always ends with '\0') */
    uint16_t cap;           /* size of buf, including the '\0' */
} outbuf_t;

extern outbuf_t console;
#define CON (&console)

void ob_init(outbuf_t *o, char *buf, uint16_t cap);

void fmt_char(outbuf_t *o, char c);
void fmt_str(outbuf_t *o, const char *s);
void fmt_safe(outbuf_t *o, const char *s);              /* unprintable characters show as ? */
void fmt_u32(outbuf_t *o, uint32_t n);
void fmt_i32(outbuf_t *o, int32_t v);
void fmt_hex8(outbuf_t *o, uint8_t v);
void fmt_hex32(outbuf_t *o, uint32_t v);
void fmt_2d(outbuf_t *o, uint32_t v);                   /* two digits: 7 -> 07 */
/* prints v / 10^decimals (decimals = 1..4), for example fmt_fixed(o, -253, 2) prints -2.53 */
void fmt_fixed(outbuf_t *o, int32_t v, uint8_t decimals);
void fmt_vec3(outbuf_t *o, const int32_t v[3], uint8_t decimals);  /* three values, spaces between */
void fmt_datetime(outbuf_t *o, uint32_t secs);          /* 2026-10-16 20:31:05 */

int      str_equal(const char *a, const char *b);
int      starts_with(const char *s, const char *prefix);
uint8_t  str_len(const char *s);
/* digits only, value must be <= max. Returns 1 if ok. */
int      parse_u32(const char *s, uint32_t *out, uint32_t max);

uint32_t crc32_str(const char *s);      /* same as Python's zlib.crc32 */
uint32_t isqrt32(uint32_t n);           /* integer square root */

#endif
