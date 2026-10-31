/*
 * USART1 console.
 * Receive: the interrupt puts every byte into a FreeRTOS stream buffer; CliTask waits on it.
 * Transmit: text is collected in one of two 128-byte buffers; a full buffer (or uart_unlock)
 *   hands it to DMA1 channel 4, which sends it while the CPU does other work. The second
 *   buffer is filled meanwhile.
 * A task must hold the UART lock while it prints: uart_lock() ... print ... uart_unlock().
 */
#ifndef UART_H
#define UART_H

#include <stdint.h>

#define UART_WAIT_FOREVER 0xFFFFFFFFU

void uart_init(void);                   /* call before the scheduler starts */
int  uart_lock(uint32_t timeout_ms);    /* 1 = this task may print now */
void uart_unlock(void);                 /* sends what is left, then frees the UART */
void uart_flush(void);                  /* start sending now, keep the lock */
int  uart_tx_idle(void);                /* 1 = everything has been sent (STOP is safe) */
void uart_putc(char c);                 /* the caller must hold the lock */
void uart_puts(const char *s);
int  uart_getc(uint8_t *out);           /* 1 = got a byte, 0 = nothing received (no wait) */
int  uart_getc_wait(uint8_t *out, uint32_t timeout_ms);   /* sleeps until a byte arrives */
void uart_rx_flush(void);               /* forget everything received so far */
/* for fault handlers only: interrupts off, no DMA, no lock, sends byte by byte */
void uart_panic_puts(const char *s);

#endif
