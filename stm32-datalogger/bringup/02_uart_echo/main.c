#include <stdint.h>

#define RCC_APB2ENR (*(volatile uint32_t *)0x40021018)
#define GPIOA_CRH   (*(volatile uint32_t *)0x40010804)
#define USART1_SR   (*(volatile uint32_t *)0x40013800)
#define USART1_DR   (*(volatile uint32_t *)0x40013804)
#define USART1_BRR  (*(volatile uint32_t *)0x40013808)
#define USART1_CR1  (*(volatile uint32_t *)0x4001380C)

static void uart_send_char(char c)
{
    while (!(USART1_SR & (1U << 7))) { }   /* wait until the UART is ready for the next byte */
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
    RCC_APB2ENR |= (1U << 2) | (1U << 14);        /* clock for GPIOA and USART1 */

    GPIOA_CRH &= ~((0xFU << 4) | (0xFU << 8));    /* clear settings of PA9 and PA10 */
    GPIOA_CRH |=  (0xBU << 4) | (0x4U << 8);      /* PA9: UART transmit pin, PA10: UART receive pin */

    USART1_BRR = 0x341;                           /* 9600 baud */
    USART1_CR1 = (1U << 13) | (1U << 3) | (1U << 2); /* UART on, transmitter on, receiver on */

    uart_send_string("Hello from STM32\r\n");

    while (1)
    {
        if (USART1_SR & (1U << 5))                /* a byte has arrived */
        {
            char c = (char)USART1_DR;
            uart_send_char(c);                    /* send it back (echo) */
            uart_send_string("\r\n");
        }
    }
}
