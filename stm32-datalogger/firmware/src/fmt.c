/* Text output into a buffer or the UART, and small text helpers. */
#include "fmt.h"
#include "rtc.h"
#include "uart.h"

outbuf_t console = { 0, 0, 0 };

void ob_init(outbuf_t *o, char *buf, uint16_t cap)
{
    o->buf = buf;
    o->len = 0;
    o->cap = cap;
    if (buf && cap) {
        buf[0] = '\0';
    }
}

void fmt_char(outbuf_t *o, char c)
{
    if (o->buf == 0) {
        uart_putc(c);
        return;
    }
    if (o->len + 1U < o->cap) {                 /* keep room for the '\0'; extra text is cut */
        o->buf[o->len++] = c;
        o->buf[o->len] = '\0';
    }
}

void fmt_str(outbuf_t *o, const char *s)
{
    while (*s) {
        fmt_char(o, *s++);
    }
}

void fmt_safe(outbuf_t *o, const char *s)
{
    while (*s) {
        fmt_char(o, (*s >= 32 && *s < 127) ? *s : '?');
        s++;
    }
}

void fmt_u32(outbuf_t *o, uint32_t n)
{
    char tmp[10];
    uint8_t i = 0;
    if (n == 0) {
        fmt_char(o, '0');
        return;
    }
    while (n > 0) {
        tmp[i++] = (char)('0' + (n % 10U));
        n /= 10U;
    }
    while (i > 0) {
        fmt_char(o, tmp[--i]);
    }
}

void fmt_i32(outbuf_t *o, int32_t v)
{
    if (v < 0) {
        fmt_char(o, '-');
        fmt_u32(o, (uint32_t)0 - (uint32_t)v);   /* also correct for the most negative value */
    } else {
        fmt_u32(o, (uint32_t)v);
    }
}

void fmt_hex8(outbuf_t *o, uint8_t v)
{
    static const char digits[] = "0123456789ABCDEF";
    fmt_char(o, digits[v >> 4]);
    fmt_char(o, digits[v & 0xFU]);
}

void fmt_hex32(outbuf_t *o, uint32_t v)
{
    fmt_hex8(o, (uint8_t)(v >> 24));
    fmt_hex8(o, (uint8_t)(v >> 16));
    fmt_hex8(o, (uint8_t)(v >> 8));
    fmt_hex8(o, (uint8_t)v);
}

void fmt_2d(outbuf_t *o, uint32_t v)
{
    fmt_char(o, (char)('0' + (v / 10U) % 10U));
    fmt_char(o, (char)('0' + v % 10U));
}

void fmt_fixed(outbuf_t *o, int32_t v, uint8_t decimals)
{
    uint32_t scale = 1U, mag, frac, digit;
    uint8_t i;

    for (i = 0; i < decimals; i++) {
        scale *= 10U;
    }
    if (v < 0) {
        fmt_char(o, '-');
        mag = (uint32_t)0 - (uint32_t)v;
    } else {
        mag = (uint32_t)v;
    }
    fmt_u32(o, mag / scale);
    fmt_char(o, '.');
    frac = mag % scale;
    for (digit = scale / 10U; digit > 1U && frac < digit; digit /= 10U) {
        fmt_char(o, '0');                       /* leading zeros of the fraction */
    }
    fmt_u32(o, frac);
}

void fmt_vec3(outbuf_t *o, const int32_t v[3], uint8_t decimals)
{
    uint8_t i;
    for (i = 0; i < 3; i++) {
        if (i > 0) {
            fmt_char(o, ' ');
        }
        fmt_fixed(o, v[i], decimals);
    }
}

void fmt_datetime(outbuf_t *o, uint32_t secs)
{
    uint32_t rem = secs % 86400U;
    int32_t y, m, d;
    date_from_days(secs / 86400U, &y, &m, &d);
    fmt_u32(o, (uint32_t)y);
    fmt_char(o, '-');
    fmt_2d(o, (uint32_t)m);
    fmt_char(o, '-');
    fmt_2d(o, (uint32_t)d);
    fmt_char(o, ' ');
    fmt_2d(o, rem / 3600U);
    fmt_char(o, ':');
    fmt_2d(o, (rem % 3600U) / 60U);
    fmt_char(o, ':');
    fmt_2d(o, rem % 60U);
}

/* ---------------- text helpers ---------------- */

int str_equal(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

int starts_with(const char *s, const char *prefix)
{
    while (*prefix) {
        if (*s++ != *prefix++) {
            return 0;
        }
    }
    return 1;
}

uint8_t str_len(const char *s)
{
    uint8_t n = 0;
    while (s[n] && n < 255U) {
        n++;
    }
    return n;
}

int parse_u32(const char *s, uint32_t *out, uint32_t max)
{
    uint32_t v = 0, d;
    if (*s == '\0') {
        return 0;                               /* no digits */
    }
    while (*s) {
        if (*s < '0' || *s > '9') {
            return 0;                           /* not a digit */
        }
        d = (uint32_t)(*s - '0');
        if (d > max || v > (max - d) / 10U) {
            return 0;                           /* bigger than max (also stops overflow) */
        }
        v = v * 10U + d;
        s++;
    }
    *out = v;
    return 1;
}

/* ---------------- numbers ---------------- */

uint32_t crc32_str(const char *s)
{
    uint32_t crc = 0xFFFFFFFFU;
    uint8_t i;
    while (*s) {
        crc ^= (uint8_t)*s++;
        for (i = 0; i < 8; i++) {
            if (crc & 1U) {
                crc = (crc >> 1) ^ 0xEDB88320U;
            } else {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}

uint32_t isqrt32(uint32_t n)
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
