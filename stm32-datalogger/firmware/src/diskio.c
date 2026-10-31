/*
 * Glue between FatFs and the SD driver. FatFs asks for whole 512-byte sectors;
 * these functions pass the requests to sd.c. Only drive 0 exists.
 * After any read or write error the drive is marked 'not ready', so FatFs stops
 * using it until the card is mounted again.
 */
#include "diskio.h"
#include "ff.h"
#include "rtc.h"
#include "sd.h"

static DSTATUS drive_status = STA_NOINIT;

DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != 0U) {
        return STA_NOINIT;
    }
    drive_status = (sd_init() == SD_OK) ? 0U : STA_NOINIT;
    return drive_status;
}

DSTATUS disk_status(BYTE pdrv)
{
    return (pdrv != 0U) ? STA_NOINIT : drive_status;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0U || count == 0U) {
        return RES_PARERR;
    }
    if (drive_status & STA_NOINIT) {
        return RES_NOTRDY;
    }
    while (count--) {
        if (sd_read_block(sector++, buff) != SD_OK) {
            drive_status = STA_NOINIT;
            return RES_ERROR;
        }
        buff += SD_BLOCK_SIZE;
    }
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0U || count == 0U) {
        return RES_PARERR;
    }
    if (drive_status & STA_NOINIT) {
        return RES_NOTRDY;
    }
    while (count--) {
        if (sd_write_block(sector++, buff) != SD_OK) {
            drive_status = STA_NOINIT;
            return RES_ERROR;
        }
        buff += SD_BLOCK_SIZE;
    }
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv != 0U) {
        return RES_PARERR;
    }
    if (drive_status & STA_NOINIT) {
        return RES_NOTRDY;
    }
    switch (cmd) {
    case CTRL_SYNC:                                   /* wait until the last write is done */
        return (sd_sync() == SD_OK) ? RES_OK : RES_ERROR;
    case GET_SECTOR_COUNT:
        *(LBA_t *)buff = sd_sector_count();
        return RES_OK;
    case GET_SECTOR_SIZE:
        *(WORD *)buff = SD_BLOCK_SIZE;
        return RES_OK;
    case GET_BLOCK_SIZE:
        *(DWORD *)buff = 1U;                          /* not known: report 1 sector */
        return RES_OK;
    default:
        return RES_PARERR;
    }
}

/* File time stamps. If the clock was never set, every file gets 2026-01-01 00:00:00
   instead of a nonsense 1970 date (FAT cannot store years before 1980). */
DWORD get_fattime(void)
{
    uint32_t secs, rem;
    int32_t y, m, d;

    if (!rtc_clock_is_set()) {
        return ((DWORD)(2026 - 1980) << 25) | ((DWORD)1 << 21) | ((DWORD)1 << 16);
    }
    secs = rtc_get();
    rem = secs % 86400U;
    date_from_days(secs / 86400U, &y, &m, &d);
    return ((DWORD)(y - 1980) << 25) | ((DWORD)m << 21) | ((DWORD)d << 16) |
           ((DWORD)(rem / 3600U) << 11) | ((DWORD)((rem % 3600U) / 60U) << 5) |
           ((DWORD)(rem % 60U) / 2U);
}
