/* USART1 on PA9 (TX) / PA10 (RX): stream buffer for receive, DMA1 channel 4 for transmit. */
#include "FreeRTOS.h"
#include "semphr.h"
#include "stream_buffer.h"
#include "board.h"
#include "config.h"
#include "locks.h"
#include "power.h"
#include "uart.h"

/* Interrupts that call FreeRTOS ...FromISR() functions must not be more urgent than
   configMAX_SYSCALL_INTERRUPT_PRIORITY (5). After reset every interrupt has priority 0, the
   most urgent, which is not allowed, so both get priority 6 here. */
#define UART_IRQ_PRIO    (6U << 4)

#define RX_SIZE          64U
#define TX_SIZE          128U       /* a whole log line fits: the logger never waits for the UART */
#define TX_TIMEOUT_MS    300U       /* 128 bytes need 133 ms at 9600 baud */
#define POLL_LIMIT       200000U    /* polled waits: about 0.1..0.2 s */

static StreamBufferHandle_t rx_stream;
static SemaphoreHandle_t    tx_done;            /* given by the DMA interrupt */
static char                 tx_buf[2][TX_SIZE];
static uint8_t              tx_fill = 0;        /* the buffer being filled */
static uint16_t             tx_len = 0;
static volatile uint8_t     dma_busy = 0;

/* ---------------- receive ---------------- */

void USART1_IRQHandler(void)
{
    BaseType_t woken = pdFALSE;
    uint32_t sr = USART1_SR;
    if (sr & (USART_SR_RXNE | USART_SR_ORE)) {
        uint8_t c = (uint8_t)USART1_DR;               /* reading DR clears the flags */
        power_console_activity();                     /* someone is typing: stay out of STOP */
        if (!(sr & USART_SR_ERRORS) && power_uart_byte_ok()) {   /* drop broken bytes */
            (void)xStreamBufferSendFromISR(rx_stream, &c, 1, &woken);   /* full: dropped */
        }
    }
    portYIELD_FROM_ISR(woken);                        /* run CliTask now if it was waiting */
}

int uart_getc(uint8_t *out)
{
    return xStreamBufferReceive(rx_stream, out, 1, 0) == 1U;
}

int uart_getc_wait(uint8_t *out, uint32_t timeout_ms)
{
    TickType_t t = (timeout_ms == UART_WAIT_FOREVER) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xStreamBufferReceive(rx_stream, out, 1, t) == 1U;
}

void uart_rx_flush(void)
{
    uint8_t c;
    while (uart_getc(&c)) { }
}

/* ---------------- transmit ---------------- */

void DMA1_Channel4_IRQHandler(void)
{
    BaseType_t woken = pdFALSE;
    if (DMA1_ISR & (DMA_ISR_TCIF4 | DMA_ISR_TEIF4)) {
        DMA1_IFCR = DMA_IFCR_CGIF4;
        DMA1_CCR4 = 0;                                /* channel off until the next buffer */
        dma_busy = 0;
        (void)xSemaphoreGiveFromISR(tx_done, &woken);
    }
    portYIELD_FROM_ISR(woken);
}

static void dma_stop(void)
{
    DMA1_CCR4 = 0;
    DMA1_IFCR = DMA_IFCR_CGIF4;
    dma_busy = 0;
}

/* the task sleeps until the DMA has sent the previous buffer */
static void dma_wait_idle(void)
{
    while (dma_busy) {
        if (xSemaphoreTake(tx_done, pdMS_TO_TICKS(TX_TIMEOUT_MS)) != pdTRUE && dma_busy) {
            dma_stop();                               /* should never happen: give up on it */
        }
    }
}

static void dma_send(const char *p, uint16_t n)
{
    DMA1_CCR4   = 0;                                  /* the channel must be off to set it up */
    DMA1_IFCR   = DMA_IFCR_CGIF4;
    DMA1_CPAR4  = USART1_DR_ADDR;
    DMA1_CMAR4  = (uint32_t)p;
    DMA1_CNDTR4 = n;
    dma_busy    = 1;
    DMA1_CCR4   = DMA_CCR_DIR | DMA_CCR_MINC | DMA_CCR_TCIE | DMA_CCR_EN;  /* 8-bit, memory -> UART */
}

static void tx_flush(void)
{
    if (tx_len == 0U) {
        return;
    }
    dma_wait_idle();                                  /* the other buffer is free after this */
    dma_send(tx_buf[tx_fill], tx_len);
    tx_fill ^= 1U;
    tx_len = 0;
}

/* byte by byte, no interrupts needed: used before the scheduler and in fault handlers */
static void polled_putc(char c)
{
    uint32_t n = 0;
    while (!(USART1_SR & USART_SR_TXE) && ++n < POLL_LIMIT) { }
    USART1_DR = (uint32_t)(uint8_t)c;
}

void uart_putc(char c)
{
    if (!rtos_running()) {
        polled_putc(c);
        return;
    }
    configASSERT(lock_held(LOCK_UART));               /* printing without uart_lock() is a bug */
    tx_buf[tx_fill][tx_len++] = c;
    if (tx_len == TX_SIZE) {
        tx_flush();
    }
}

void uart_puts(const char *s)
{
    while (*s) {
        uart_putc(*s++);
    }
}

int uart_tx_idle(void)                    /* nothing waiting, DMA done, last bit out of the pin */
{
    return tx_len == 0U && !dma_busy && (USART1_SR & USART_SR_TC);
}

int uart_lock(uint32_t timeout_ms)
{
    return lock_take(LOCK_UART, timeout_ms == UART_WAIT_FOREVER ? LOCK_FOREVER : timeout_ms);
}

void uart_flush(void)
{
    if (rtos_running()) {
        tx_flush();                                   /* the DMA finishes it in the background */
    }
}

void uart_unlock(void)
{
    uart_flush();
    lock_give(LOCK_UART);
}

void uart_panic_puts(const char *s)
{
    uint32_t n = 0;
    while (dma_busy && !(DMA1_ISR & DMA_ISR_TCIF4) && ++n < POLL_LIMIT) { }
    dma_stop();
    if (tx_len > 0U) {                                /* text that was waiting in the buffer */
        uint16_t i;
        for (i = 0; i < tx_len; i++) {
            polled_putc(tx_buf[tx_fill][i]);
        }
        tx_len = 0;
    }
    while (*s) {
        polled_putc(*s++);
    }
    n = 0;
    while (!(USART1_SR & USART_SR_TXE) && ++n < POLL_LIMIT) { }
}

void uart_init(void)
{
    rx_stream = xStreamBufferCreate(RX_SIZE, 1);      /* CliTask wakes up for every byte */
    tx_done   = xSemaphoreCreateBinary();
    configASSERT(rx_stream != NULL && tx_done != NULL);

    USART1_BRR = UART_BRR_VALUE;
    USART1_CR3 = USART_CR3_DMAT;
    USART1_CR1 = USART_CR1_UE | USART_CR1_RXNEIE | USART_CR1_TE | USART_CR1_RE;

    NVIC_IPR(IRQ_USART1)   = UART_IRQ_PRIO;
    NVIC_IPR(IRQ_DMA1_CH4) = UART_IRQ_PRIO;
    NVIC_ISER1 = 1U << (IRQ_USART1 - 32U);
    NVIC_ISER0 = 1U << IRQ_DMA1_CH4;
}
