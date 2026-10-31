#include <stdint.h>

#define RCC_APB2ENR (*(volatile uint32_t *)0x40021018)
#define GPIOA_CRH   (*(volatile uint32_t *)0x40010804)
#define USART1_SR   (*(volatile uint32_t *)0x40013800)
#define USART1_DR   (*(volatile uint32_t *)0x40013804)
#define USART1_BRR  (*(volatile uint32_t *)0x40013808)
#define USART1_CR1  (*(volatile uint32_t *)0x4001380C)
#define NVIC_ISER1  (*(volatile uint32_t *)0xE000E104)

#define RX_SIZE 64U                       /* must be a power of two */

static volatile uint8_t  rx_buf[RX_SIZE];
static volatile uint8_t  rx_head = 0;     /* only the interrupt changes this */
static volatile uint8_t  rx_tail = 0;     /* only main changes this */
static volatile uint32_t rx_overflow = 0; /* counts bytes lost when full */

void USART1_IRQHandler(void)
{
    if (USART1_SR & (1U << 5)) {                  /* a byte has arrived */
        uint8_t c = (uint8_t)USART1_DR;           /* reading DR clears the flag */
        uint8_t next = (rx_head + 1U) & (RX_SIZE - 1U);
        if (next != rx_tail) {                    /* room left? */
            rx_buf[rx_head] = c;
            rx_head = next;
        } else {
            rx_overflow++;                        /* full: drop the byte */
        }
    }
}

static int rx_pop(uint8_t *out)
{
    if (rx_tail == rx_head) {
        return 0;                                 /* empty */
    }
    *out = rx_buf[rx_tail];
    rx_tail = (rx_tail + 1U) & (RX_SIZE - 1U);
    return 1;
}

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

int main(void)
{
    RCC_APB2ENR |= (1U << 2) | (1U << 14);

    GPIOA_CRH &= ~((0xFU << 4) | (0xFU << 8));
    GPIOA_CRH |=  (0xBU << 4) | (0x4U << 8);

    USART1_BRR = 0x45;                                    /* 115200 baud (8 MHz / 69) */
    USART1_CR1 = (1U << 13) | (1U << 5) | (1U << 3) | (1U << 2);
                 /* UART on, receive interrupt on, transmitter on, receiver on */

    NVIC_ISER1 = (1U << 5);          /* enable USART1 interrupt: IRQ 37 = 32 + 5 */

    uart_send_string("RX IRQ TESTTTT\r\n");

    while (1)
    {
        uint8_t c;
        if (rx_pop(&c)) {
            uart_send_char((char)c);              /* echo it back */
            if (c == '\r') {
                uart_send_char('\n');             /* Enter starts a new line */
            }
        }
    }
}
