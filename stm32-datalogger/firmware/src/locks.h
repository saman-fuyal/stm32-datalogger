/*
 * The three shared things that tasks must not use at the same time, each with a mutex:
 *   LOCK_UART  the serial port output (so lines from two tasks never mix)
 *   LOCK_I2C   the I2C bus and the two sensor drivers
 *   LOCK_SD    the SPI bus, the SD card, FatFs and the log file
 *
 * They are recursive mutexes: a task that holds a lock may take it again (for example a
 * command holds LOCK_UART and calls a function that also takes it). Give it back as many
 * times as it was taken.
 *
 * Rule against deadlock: take them in the order UART -> I2C -> SD. A task that already
 * holds I2C or SD takes UART only with a timeout (it skips the message if it must wait).
 *
 * FreeRTOS mutexes have priority inheritance: while a low priority task holds a lock that a
 * higher priority task waits for, the holder runs at the higher priority (see README).
 */
#ifndef LOCKS_H
#define LOCKS_H

#include <stdint.h>

typedef enum { LOCK_UART, LOCK_I2C, LOCK_SD, LOCK_COUNT } lock_id_t;

#define LOCK_FOREVER 0xFFFFFFFFU

void locks_init(void);                              /* creates the mutexes (before the scheduler) */
int  lock_take(lock_id_t id, uint32_t timeout_ms);  /* 1 = got it, 0 = timed out */
void lock_give(lock_id_t id);
int  lock_held(lock_id_t id);                       /* 1 if the calling task holds it */

#endif
