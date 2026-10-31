#include <stdint.h>

#define RCC_APB2ENR (*(volatile uint32_t *)0x40021018)
#define GPIOA_CRH   (*(volatile uint32_t *)0x40010804)
#define GPIOC_CRH   (*(volatile uint32_t *)0x40011004)
#define GPIOC_ODR   (*(volatile uint32_t *)0x4001100C)
#define USART1_SR   (*(volatile uint32_t *)0x40013800)
#define USART1_DR   (*(volatile uint32_t *)0x40013804)
#define USART1_BRR  (*(volatile uint32_t *)0x40013808)
#define USART1_CR1  (*(volatile uint32_t *)0x4001380C)
#define NVIC_ISER1  (*(volatile uint32_t *)0xE000E104)
#define STK_CTRL (*(volatile uint32_t *)0xE000E010)
#define STK_LOAD (*(volatile uint32_t *)0xE000E014)
#define STK_VAL  (*(volatile uint32_t *)0xE000E018)
/* ---------- receive ring buffer (same as before) ---------- */
#define RX_SIZE 64U
static volatile uint8_t  rx_buf[RX_SIZE];
static volatile uint8_t  rx_head = 0;
static volatile uint8_t  rx_tail = 0;

void USART1_IRQHandler(void)
{
    if (USART1_SR & (1U << 5)) {
        uint8_t c = (uint8_t)USART1_DR;
        uint8_t next = (rx_head + 1U) & (RX_SIZE - 1U);
        if (next != rx_tail) {
            rx_buf[rx_head] = c;
            rx_head = next;
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

/* ---------- sending ---------- */
static void uart_send_char(char c)
{
    while (!(USART1_SR & (1U << 7))) { }
    USART1_DR = c;
}

static void uart_send_string(const char *s)
{
    while (*s) {
        uart_send_char(*s++);
    }
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

/* ---------- tiny text helpers ---------- */
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

static int parse_number(const char *s, uint32_t *out)
{
    uint32_t v = 0;
    if (*s == '\0') {
        return 0;                       /* no digits */
    }
    while (*s) {
        if (*s < '0' || *s > '9') {
            return 0;                   /* not a digit */
        }
        v = v * 10U + (uint32_t)(*s - '0');
        if (v > 100000U) {
            return 0;                   /* too big */
        }
        s++;
    }
    *out = v;
    return 1;
}

/* ---------- the command line ---------- */
#define LINE_SIZE 32U
static char    line[LINE_SIZE];
static uint8_t line_len = 0;

static uint8_t  running = 0;
static uint32_t rate_ms = 1000;
static volatile uint32_t ms_ticks = 0;     /* milliseconds since power-on */
static uint32_t last_toggle = 0;           /* when the LED last changed */

void SysTick_Handler(void)
{
    ms_ticks++;
}
static void handle_line(const char *cmd)
{
    uint32_t n;

    if (cmd[0] == '\0') {
        return;                         /* empty line: ignore */
    }

    if (str_equal(cmd, "start")) {
        running = 1;
        last_toggle = ms_ticks;
        GPIOC_ODR &= ~(1U << 13);       /* LED on (it is active-low) */
        uart_send_string("OK started\r\n");
    } else if (str_equal(cmd, "stop")) {
        running = 0;
        GPIOC_ODR |= (1U << 13);        /* LED off */
        uart_send_string("OK stopped\r\n");
    } else if (starts_with(cmd, "set rate ") &&
               parse_number(cmd + 9, &n) && n >= 10U && n <= 60000U) {
        rate_ms = n;
        uart_send_string("OK rate = ");
        uart_send_number(rate_ms);
        uart_send_string(" ms\r\n");
    } else if (str_equal(cmd, "status")) {
        uart_send_string(running ? "running, rate = " : "stopped, rate = ");
        uart_send_number(rate_ms);
        uart_send_string(" ms\r\n");
    } else {
        uart_send_string("ERR unknown command\r\n");
    }
}

int main(void)
{
    RCC_APB2ENR |= (1U << 2) | (1U << 4) | (1U << 14);   /* GPIOA, GPIOC, USART1 */

    GPIOA_CRH &= ~((0xFU << 4) | (0xFU << 8));
    GPIOA_CRH |=  (0xBU << 4) | (0x4U << 8);

    GPIOC_CRH &= ~(0xFU << 20);          /* PC13 = LED output */
    GPIOC_CRH |=  (0x2U << 20);
    GPIOC_ODR |=  (1U << 13);            /* LED off at start */

    USART1_BRR = 0x341;                                      /* 9600 baud */
    USART1_CR1 = (1U << 13) | (1U << 5) | (1U << 3) | (1U << 2);
    NVIC_ISER1 = (1U << 5);
    STK_LOAD = 8000U - 1U;     /* 8 MHz / 8000 = 1000 ticks per second */
    STK_VAL  = 0;
    STK_CTRL = 7U;             /* timer on, interrupt on, use CPU clock */

    uart_send_string("CLI ready. Commands: start, stop, set rate N, status\r\n> ");

    while (1)
    {
        if (running && (ms_ticks - last_toggle) >= rate_ms) {
            last_toggle = ms_ticks;
            GPIOC_ODR ^= (1U << 13);       /* flip the LED */
        }
        uint8_t c;
        if (rx_pop(&c)) {
            if (c == '\r' || c == '\n') {              /* Enter pressed */
                uart_send_string("\r\n");
                line[line_len] = '\0';
                handle_line(line);
                line_len = 0;
                uart_send_string("> ");
            } else if (c == 0x08 || c == 0x7F) {       /* backspace */
                if (line_len > 0) {
                    line_len--;
                    uart_send_string("\b \b");
                }
            } else if (line_len < LINE_SIZE - 1U) {    /* normal letter */
                line[line_len++] = (char)c;
                uart_send_char((char)c);
            }
        }
    }
}
