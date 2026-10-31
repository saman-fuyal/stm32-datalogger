/*
 * The log file on the SD card (FatFs).
 * Each logging session gets a new file LOG0001.CSV, LOG0002.CSV, ...
 * The file is synced (made safe against power loss) every 10 lines or every 10 seconds.
 * If a write fails (card pulled out, supply dip) the file is dropped and the card
 * counts as not mounted; the logger can try again later.
 * FatFs is not thread-safe: hold LOCK_SD (locks.h) around every call to these functions.
 */
#ifndef SDLOG_H
#define SDLOG_H

#include <stdint.h>

#define SDLOG_SYNC_LINES 10U
#define SDLOG_SYNC_MS    10000U

int         sdlog_mount(void);              /* 1 = card mounted */
int         sdlog_mounted(void);
int         sdlog_open_new(const char *header);   /* next free LOGnnnn.CSV, writes header. 1 = ok */
int         sdlog_is_open(void);
const char *sdlog_filename(void);           /* "" when no file is open */
int         sdlog_write(const char *text);  /* 1 = ok, 0 = failed (the file was dropped) */
void        sdlog_sync_if_due(void);        /* call often: syncs after 10 s without a sync */
#define SDLOG_NO_SYNC_DUE 0xFFFFFFFFU
uint32_t    sdlog_ms_until_sync(void);      /* when sdlog_sync_if_due has work, or SDLOG_NO_SYNC_DUE */
int         sdlog_sync(void);
void        sdlog_close(void);
uint32_t    sdlog_lines(void);              /* lines written to the open file */
uint32_t    sdlog_errors(void);             /* write/sync failures since power-on */
int         sdlog_last_result(void);        /* last FatFs result code (FRESULT) */
const char *sdlog_result_text(int fr);

#endif
