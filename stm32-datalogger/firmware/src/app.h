/*
 * The FreeRTOS tasks of the data logger.
 *
 *   Task      Priority  Job
 *   Start     4         checks every part at power-on, creates the others, then deletes itself
 *   Sensor    3         reads the sensors every 'rate' ms, sends a record to the queue
 *   Logger    2         takes records from the queue, prints them, writes them to the SD card
 *   Cli       1         the command line (waits for keys from the UART stream buffer)
 *   IDLE      0         FreeRTOS's own task, runs when nothing else needs the CPU
 *
 * The sensor gets the highest priority so that samples are taken on time even while the
 * SD card is slow (a write can take 100 ms or more); the queue holds the records meanwhile.
 */
#ifndef APP_H
#define APP_H

#include <stdint.h>

void     app_start(void);               /* creates the start-up task and starts the scheduler */
void     app_wake_sensor(void);         /* start, stop or a new rate: SensorTask re-plans now */
void     app_flush_records(void);       /* empty the queue */
uint32_t app_dropped_records(void);     /* records lost because the queue was full */

#endif
