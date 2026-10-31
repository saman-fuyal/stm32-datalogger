/* The log file on the SD card, through FatFs. */
#include "board.h"
#include "ff.h"
#include "fmt.h"
#include "sdlog.h"

static FATFS    fs;
static FIL      file;
static uint8_t  mounted = 0;
static uint8_t  file_open = 0;
static char     filename[13] = "";            /* 8.3 name + '\0' */
static uint32_t lines = 0;
static uint32_t lines_since_sync = 0;
static uint32_t last_sync_ms = 0;
static uint32_t errors = 0;
static FRESULT  last_fr = FR_OK;

static void drop_card(void)                   /* after an error: forget the file and the card */
{
    file_open = 0;
    mounted = 0;
    filename[0] = '\0';
    (void)f_mount(0, "", 0);
}

int sdlog_mount(void)
{
    if (file_open) {
        return 1;                             /* busy logging: it is mounted */
    }
    last_fr = f_mount(&fs, "", 1);            /* 1 = mount now (this starts the card) */
    mounted = (last_fr == FR_OK);
    if (!mounted) {
        (void)f_mount(0, "", 0);
    }
    return mounted;
}

int sdlog_mounted(void)
{
    return mounted;
}

/* "LOG0042.CSV" -> 42, anything else -> 0 */
static uint32_t log_number(const char *name)
{
    uint32_t n = 0;
    uint8_t i;
    if (!starts_with(name, "LOG") || !str_equal(name + 7, ".CSV")) {
        return 0;
    }
    for (i = 3; i < 7U; i++) {
        if (name[i] < '0' || name[i] > '9') {
            return 0;
        }
        n = n * 10U + (uint32_t)(name[i] - '0');
    }
    return n;
}

static uint32_t next_log_number(void)
{
    DIR dir;
    FILINFO info;
    uint32_t n, max = 0;

    if (f_opendir(&dir, "/") != FR_OK) {
        return 1;
    }
    while (f_readdir(&dir, &info) == FR_OK && info.fname[0] != '\0') {
        n = log_number(info.fname);
        if (n > max) {
            max = n;
        }
    }
    (void)f_closedir(&dir);
    return max + 1U;
}

int sdlog_open_new(const char *header)
{
    outbuf_t o;
    uint32_t n;
    UINT bw;

    if (file_open) {
        sdlog_close();
    }
    if (!mounted && !sdlog_mount()) {
        return 0;
    }
    n = next_log_number();
    if (n > 9999U) {
        last_fr = FR_DENIED;                  /* card full of log files */
        return 0;
    }
    ob_init(&o, filename, sizeof(filename));
    fmt_str(&o, "LOG");
    fmt_char(&o, (char)('0' + (n / 1000U) % 10U));
    fmt_char(&o, (char)('0' + (n / 100U) % 10U));
    fmt_2d(&o, n % 100U);
    fmt_str(&o, ".CSV");

    last_fr = f_open(&file, filename, FA_WRITE | FA_CREATE_NEW);
    if (last_fr != FR_OK) {
        errors++;
        drop_card();
        return 0;
    }
    file_open = 1;
    lines = 0;
    last_fr = f_write(&file, header, str_len(header), &bw);
    if (last_fr == FR_OK) {
        last_fr = f_write(&file, "\r\n", 2, &bw);
    }
    if (last_fr != FR_OK) {
        errors++;
        drop_card();
        return 0;
    }
    return sdlog_sync();                      /* the file now exists on the card */
}

int sdlog_is_open(void)
{
    return file_open;
}

const char *sdlog_filename(void)
{
    return filename;
}

int sdlog_sync(void)
{
    if (!file_open) {
        return 0;
    }
    last_fr = f_sync(&file);                  /* data, FAT and directory entry go to the card */
    lines_since_sync = 0;
    last_sync_ms = tick_ms();
    if (last_fr != FR_OK) {
        errors++;
        drop_card();
        return 0;
    }
    return 1;
}

int sdlog_write(const char *text)
{
    UINT len, bw;

    if (!file_open) {
        return 0;
    }
    len = str_len(text);
    last_fr = f_write(&file, text, len, &bw);
    if (last_fr != FR_OK || bw != len) {
        if (last_fr == FR_OK) {
            last_fr = FR_DENIED;              /* the card is full */
        }
        errors++;
        drop_card();
        return 0;
    }
    lines++;
    if (++lines_since_sync >= SDLOG_SYNC_LINES) {
        return sdlog_sync();
    }
    return 1;
}

uint32_t sdlog_ms_until_sync(void)
{
    uint32_t since;
    if (!file_open || lines_since_sync == 0U) {
        return SDLOG_NO_SYNC_DUE;
    }
    since = tick_ms() - last_sync_ms;
    return since >= SDLOG_SYNC_MS ? 0U : SDLOG_SYNC_MS - since;
}

void sdlog_sync_if_due(void)
{
    if (file_open && lines_since_sync > 0U && (tick_ms() - last_sync_ms) >= SDLOG_SYNC_MS) {
        (void)sdlog_sync();
    }
}

void sdlog_close(void)
{
    if (!file_open) {
        return;
    }
    last_fr = f_close(&file);
    file_open = 0;
    if (last_fr != FR_OK) {
        errors++;
        drop_card();
    }
}

uint32_t sdlog_lines(void)
{
    return lines;
}

uint32_t sdlog_errors(void)
{
    return errors;
}

int sdlog_last_result(void)
{
    return (int)last_fr;
}

const char *sdlog_result_text(int fr)
{
    switch (fr) {
    case FR_OK:             return "ok";
    case FR_DISK_ERR:       return "disk error (card removed or a write failed)";
    case FR_NOT_READY:      return "card not ready (no card, or it did not start)";
    case FR_NO_FILE:        return "file not found";
    case FR_NO_FILESYSTEM:  return "no FAT file system (format the card as FAT32)";
    case FR_DENIED:         return "denied (card full?)";
    case FR_EXIST:          return "file already exists";
    case FR_WRITE_PROTECTED: return "card is write-protected";
    default:                return "FatFs error";
    }
}
